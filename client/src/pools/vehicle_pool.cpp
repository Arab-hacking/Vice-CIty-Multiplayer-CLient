// Серверные машины. Создание повторяет путь самой игры (видно из дизассемблера
// вызовов в CCarCtrl::GenerateOneRandomCar и др.):
//   CPools::ms_pVehiclePool->New()          -> слот в пуле транспорта
//   CAutomobile::CAutomobile(model, 2)      -> 2 = MISSION_VEHICLE (не трафик:
//                                              CCarCtrl не удалит как случайную)
//   поза: встроенная матрица CPlaceable (veh+0x08, pos = veh+0x38) +
//         SetHeading(this=veh+8) + копия матрицы в матрицу Rw-фрейма
//         (CMatrix::pMatrix на veh+0x48) + CEntity::UpdateRwFrame
//   CWorld::Add(veh)                        -> в мир
// Удаление: CWorld::Remove + scalar deleting destructor (vtable[0]).
// Всё под SEH: битая модель не должна ронять игру.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <map>
#include <set>

#include "../core/seh_guard.h"
#include "../game/revc.h"
#include "../network/net_client.h"
#include "vehicle_pool.h"

namespace vc::pools {
namespace {

using PoolNewFn = void* (*)(void*);
using AutomobileCtorFn = void* (*)(void*, int, unsigned char);
using WorldAddRemoveFn = void (*)(void*);
using SetHeadingFn = void (*)(void*, float);
using UpdateRwFrameFn = void (*)(void*);
using RequestModelFn = void (*)(int, int);
using LoadAllModelsFn = void (*)(bool);
using DeletingDtorFn = void* (*)(void*, unsigned int);

constexpr unsigned char kMissionVehicle = 2; // eCreatedBy: RANDOM=1, MISSION=2
constexpr int kModelWarmupFrames = 30;
// CStreaming НЕ держит модель за сущность (SetModelIndex без AddRef): модель без
// флагов DONT_REMOVE(1)|SCRIPTOWNED(2) выгружается по LRU вместе с коллизией —
// машина проваливается под землю и «исчезает». Трафик/парковки игра спасает
// повторными запросами; мы ставим флаги и тоже пере-запрашиваем периодически.
constexpr int kStreamFlagsKeep = 3;
constexpr int kMaxCreateAttempts = 3;
constexpr long long kRetryDelayTicks = 300; // ~5 секунд между попытками создания

FILE* g_log = nullptr;

struct SpawnedVehicle {
    void* vehicle = nullptr;
    std::uint16_t model = 0;
    vc::Vector3 spawnPos{};
    float spawnHeading = 0.0f;
    int attempts = 0;                 // неудачных попыток создания
    long long nextRetryTick = 0;      // раньше этого кадра не повторять
    bool fellReported = false;        // watchdog уже ругался в лог
};

std::map<std::uint16_t, SpawnedVehicle> g_vehicles;
std::map<int, int> g_modelWarmup; // modelId -> кадров загрузки прошло
long long g_tickCount = 0;

void Log(const char* text) {
    OutputDebugStringA(text);
    if (g_log) {
        std::fputs(text, g_log);
        std::fflush(g_log);
    }
}

// True, когда модель запрошена и ей дали время загрузиться.
bool EnsureModelReady(int modelId) {
    auto [it, inserted] = g_modelWarmup.try_emplace(modelId, 0);
    if (inserted) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "vpool: запрашиваю модель %d\n", modelId);
        Log(buf);
        game::Resolve<RequestModelFn>(game::revc::kCStreamingRequestModel)(
            modelId, kStreamFlagsKeep);
        return false;
    }
    if (it->second < kModelWarmupFrames) {
        game::Resolve<LoadAllModelsFn>(game::revc::kCStreamingLoadAllRequested)(false);
        ++it->second;
        return false;
    }
    return true;
}

// Поза машины: позиция во встроенной матрице (pos = +0x38), SetHeading(this=+8)
// её сохраняет; копию матрицы — в Rw-фрейм, иначе модель не там, где коллизия.
// addToWorld=false — машина уже в мире (watchdog-перестановка делает Remove/Add сам).
bool PlaceVehicleRaw(void* vehicle, float x, float y, float z, float heading) {
    VC_TRY {
        auto* base = static_cast<char*>(vehicle);
        *reinterpret_cast<float*>(base + 0x38) = x;
        *reinterpret_cast<float*>(base + 0x3C) = y;
        *reinterpret_cast<float*>(base + 0x40) = z;
        game::Resolve<SetHeadingFn>(game::revc::kCPlaceableSetHeading)(
            base + game::kPlaceableOffset, heading);
        void* frameMatrix =
            *reinterpret_cast<void**>(base + game::kFrameMatrixPtrOffset);
        if (frameMatrix) std::memcpy(frameMatrix, base + game::kEmbeddedMatrixOffset, 0x40);
        game::Resolve<UpdateRwFrameFn>(game::revc::kCEntityUpdateRwFrame)(vehicle);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// Прочитать текущую позицию машины (watchdog). False — указатель битый.
bool ReadVehiclePosRaw(void* vehicle, float* out) {
    VC_TRY {
        const auto* base = static_cast<const char*>(vehicle);
        out[0] = *reinterpret_cast<const float*>(base + 0x38);
        out[1] = *reinterpret_cast<const float*>(base + 0x3C);
        out[2] = *reinterpret_cast<const float*>(base + 0x40);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// POD SEH-создание машины. *failStage: 1 = пул полон (New вернул null),
// 2 = конструктор упал/вернул null (модель не автомобиль: лодка/вертолёт/байк?).
void* CreateVehicleRaw(void* pool, int model, float x, float y, float z, float heading,
                       int* failStage) {
    *failStage = 2;
    VC_TRY {
        void* memory = game::Resolve<PoolNewFn>(game::revc::kCPoolVehicleNew)(pool);
        if (!memory) {
            *failStage = 1;
            return nullptr;
        }
        void* vehicle = game::Resolve<AutomobileCtorFn>(game::revc::kCAutomobileCtor)(
            memory, model, kMissionVehicle);
        if (!vehicle) return nullptr;
        PlaceVehicleRaw(vehicle, x, y, z, heading);
        game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldAdd)(vehicle);
        *failStage = 0;
        return vehicle;
    } VC_EXCEPT {
        return nullptr;
    }
}

void* CreateVehicle(const vc::network::NetClient::RemoteVehicle& remote) {
    auto** poolPtr = game::Resolve<void**>(game::revc::kCPoolsVehiclePool);
    void* pool = poolPtr ? *poolPtr : nullptr;
    if (!pool) {
        Log("vpool: ms_pVehiclePool == nullptr (мир ещё не готов)\n");
        return nullptr;
    }
    int failStage = 0;
    void* vehicle = CreateVehicleRaw(pool, remote.model, remote.position.x,
                                       remote.position.y, remote.position.z, remote.heading,
                                       &failStage);
    if (!vehicle) {
        char buf[128];
        if (failStage == 1)
            std::snprintf(buf, sizeof(buf), "vpool: пул транспорта полон (модель %u)\n",
                          remote.model);
        else
            std::snprintf(buf, sizeof(buf),
                          "vpool: конструктор CAutomobile упал на модели %u "
                          "(это не автомобиль?)\n",
                          remote.model);
        Log(buf);
    }
    return vehicle;
}

void DestroyVehicleRaw(void* vehicle) {
    VC_TRY {
        game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldRemove)(vehicle);
        void** vtable = *reinterpret_cast<void***>(vehicle);
        reinterpret_cast<DeletingDtorFn>(vtable[0])(vehicle, 1);
    } VC_EXCEPT {
    }
}

} // namespace

VehiclePool& VehiclePool::Instance() {
    static VehiclePool instance;
    return instance;
}

void VehiclePool::Init(FILE* log) { g_log = log; }

void VehiclePool::Tick() {
    ++g_tickCount;
    const auto remotes = vc::network::NetClient::Instance().RemoteVehicles();

    // Удалить машины, которых сервер больше не показывает.
    std::set<std::uint16_t> wanted;
    for (const auto& remote : remotes) wanted.insert(remote.id);
    for (auto it = g_vehicles.begin(); it != g_vehicles.end();) {
        if (wanted.contains(it->first)) {
            ++it;
            continue;
        }
        if (it->second.vehicle) {
            DestroyVehicleRaw(it->second.vehicle);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "vpool: машина id %u удалена\n", it->first);
            Log(buf);
        }
        it = g_vehicles.erase(it);
    }
    if (remotes.empty()) return;

    // Спавнить можно только когда локальный игрок уже в мире.
    auto findPed = game::Resolve<game::FindPlayerPedFn>(game::revc::kFindPlayerPed);
    if (!findPed || !findPed()) return;

    for (const auto& remote : remotes) {
        SpawnedVehicle& spawned = g_vehicles[remote.id];
        if (spawned.vehicle && spawned.model != remote.model) {
            DestroyVehicleRaw(spawned.vehicle);
            spawned.vehicle = nullptr;
            spawned.attempts = 0;
        }
        if (spawned.vehicle) {
            // Модель надо держать загруженной, как это делают генераторы парковок:
            // пере-запрос раз в ~0.5 с флагами «не выгружать».
            if (g_tickCount % 30 == 0)
                game::Resolve<RequestModelFn>(game::revc::kCStreamingRequestModel)(
                    remote.model, kStreamFlagsKeep);
            // Watchdog: машина не должна проваливаться под землю. Если провалилась
            // (коллизию всё же выгрузили / физика вытолкнула) — лог + перестановка.
            if (g_tickCount % 120 == 0) {
                float pos[3] = {};
                if (ReadVehiclePosRaw(spawned.vehicle, pos) &&
                    pos[2] < spawned.spawnPos.z - 8.0f) {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf),
                                  "vpool: машина id %u провалилась (z=%.1f, спавн %.1f) — "
                                  "возвращаю на место\n",
                                  remote.id, pos[2], spawned.spawnPos.z);
                    Log(buf);
                    spawned.fellReported = true;
                    game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldRemove)(
                        spawned.vehicle);
                    PlaceVehicleRaw(spawned.vehicle, spawned.spawnPos.x, spawned.spawnPos.y,
                                    spawned.spawnPos.z, spawned.spawnHeading);
                    game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldAdd)(spawned.vehicle);
                }
            }
            continue;
        }
        if (spawned.attempts >= kMaxCreateAttempts) continue; // сдалиcь, в лог уже писали
        if (g_tickCount < spawned.nextRetryTick) continue;
        if (!EnsureModelReady(remote.model)) continue;
        spawned.vehicle = CreateVehicle(remote);
        if (!spawned.vehicle) {
            ++spawned.attempts;
            spawned.nextRetryTick = g_tickCount + kRetryDelayTicks;
            if (spawned.attempts >= kMaxCreateAttempts) {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "vpool: машина id %u (модель %u) не создалась за %d попыток\n",
                              remote.id, remote.model, kMaxCreateAttempts);
                Log(buf);
            }
            continue;
        }
        spawned.model = remote.model;
        spawned.spawnPos = remote.position;
        spawned.spawnHeading = remote.heading;
        spawned.attempts = 0;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "vpool: машина id %u (модель %u) создана\n",
                      remote.id, remote.model);
        Log(buf);
    }
}

} // namespace vc::pools
