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
    void* vtable = nullptr; // сверка «слот пула ещё наш» (игра могла удалить машину)
    std::uint16_t model = 0;
    vc::Vector3 spawnPos{};
    float spawnHeading = 0.0f;
    long long spawnTick = 0;
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

// Прочитать позицию + флаги машины (телеметрия). False — указатель битый.
bool ReadVehiclePosRaw(void* vehicle, float* out, unsigned int* flagsOut) {
    VC_TRY {
        const auto* base = static_cast<const char*>(vehicle);
        out[0] = *reinterpret_cast<const float*>(base + 0x38);
        out[1] = *reinterpret_cast<const float*>(base + 0x3C);
        out[2] = *reinterpret_cast<const float*>(base + 0x40);
        *flagsOut = *reinterpret_cast<const unsigned int*>(base + 0x60);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// Слот пула всё ещё «наш»? Игра могла удалить машину и отдать слот трафику:
// трогать такой объект (Remove/деструктор) нельзя — краш/порча секторов мира.
bool VehicleStillOursRaw(void* vehicle, void* vtable, std::uint16_t model) {
    VC_TRY {
        const auto* base = static_cast<const char*>(vehicle);
        void* nowVtable = *reinterpret_cast<void* const*>(base);
        const auto nowModel = *reinterpret_cast<const std::uint16_t*>(base + 0x6C);
        return nowVtable == vtable && nowModel == model;
    } VC_EXCEPT {
        return false;
    }
}

void* ReadVehicleVtableRaw(void* vehicle) {
    VC_TRY {
        return *reinterpret_cast<void**>(vehicle);
    } VC_EXCEPT {
        return nullptr;
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
        // Высота покоя как у генераторов парковок: z земли (ноги игрока от
        // сервера) + расстояние от центра масс до низа модели.
        using DistToBaseFn = float (*)(void*);
        float dist = game::Resolve<DistToBaseFn>(game::revc::kCEntityDistToBase)(vehicle);
        if (!(dist >= 0.0f && dist <= 3.0f)) dist = 0.5f;
        PlaceVehicleRaw(vehicle, x, y, z + dist, heading);
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

// Безопасное удаление: только если слот пула всё ещё наш. Если игра сама
// удалила машину — просто перестаём отслеживать (второй деструктор = краш).
void DestroyVehicle(SpawnedVehicle& spawned, std::uint16_t id) {
    if (!spawned.vehicle) return;
    if (VehicleStillOursRaw(spawned.vehicle, spawned.vtable, spawned.model)) {
        DestroyVehicleRaw(spawned.vehicle);
    } else {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "vpool: машина id %u уже удалена игрой — пропускаю деструктор\n", id);
        Log(buf);
    }
    spawned.vehicle = nullptr;
    spawned.vtable = nullptr;
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
            DestroyVehicle(it->second, it->first);
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
            DestroyVehicle(spawned, remote.id);
            spawned.attempts = 0;
        }
        if (spawned.vehicle) {
            if (g_tickCount % 30 == 0) {
                // Слот ещё наш? Если игра удалила машину — не трогаем её больше.
                if (!VehicleStillOursRaw(spawned.vehicle, spawned.vtable, spawned.model)) {
                    char buf[128];
                    std::snprintf(buf, sizeof(buf),
                                  "vpool: игра удалила машину id %u через ~%lld кадров\n",
                                  remote.id, g_tickCount - spawned.spawnTick);
                    Log(buf);
                    spawned.vehicle = nullptr;
                    spawned.vtable = nullptr;
                    spawned.attempts = kMaxCreateAttempts; // не пересоздавать в цикле
                    continue;
                }
                // Модель надо держать загруженной, как это делают генераторы
                // парковок: пере-запрос раз в ~0.5 с флагами «не выгружать».
                game::Resolve<RequestModelFn>(game::revc::kCStreamingRequestModel)(
                    remote.model, kStreamFlagsKeep);
            }
            // Телеметрия первых ~30 секунд + предупреждение о провале (БЕЗ
            // вмешательства в физику — перестановка в 0.6.10 довела до краша).
            if (g_tickCount % 60 == 0 && g_tickCount - spawned.spawnTick <= 1800) {
                float pos[3] = {};
                unsigned int flags = 0;
                if (ReadVehiclePosRaw(spawned.vehicle, pos, &flags)) {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf),
                                  "vpool-diag: id %u pos=(%.1f, %.1f, %.1f) flags=0x%x\n",
                                  remote.id, pos[0], pos[1], pos[2], flags);
                    Log(buf);
                    if (pos[2] < spawned.spawnPos.z - 8.0f && !spawned.fellReported) {
                        spawned.fellReported = true;
                        std::snprintf(buf, sizeof(buf),
                                      "vpool: машина id %u проваливается! "
                                      "(z=%.1f при спавне %.1f)\n",
                                      remote.id, pos[2], spawned.spawnPos.z);
                        Log(buf);
                    }
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
        spawned.vtable = ReadVehicleVtableRaw(spawned.vehicle);
        spawned.spawnTick = g_tickCount;
        spawned.spawnPos = remote.position;
        {
            float pos[3] = {};
            unsigned int flags = 0;
            if (ReadVehiclePosRaw(spawned.vehicle, pos, &flags))
                spawned.spawnPos = vc::Vector3{pos[0], pos[1], pos[2]};
        }
        spawned.spawnHeading = remote.heading;
        spawned.attempts = 0;
        spawned.fellReported = false;
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "vpool: машина id %u (модель %u) создана в (%.1f, %.1f, %.1f)\n",
                      remote.id, remote.model, spawned.spawnPos.x, spawned.spawnPos.y,
                      spawned.spawnPos.z);
        Log(buf);
    }
}

} // namespace vc::pools
