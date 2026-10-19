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

FILE* g_log = nullptr;

struct SpawnedVehicle {
    void* vehicle = nullptr;
    std::uint16_t model = 0;
};

std::map<std::uint16_t, SpawnedVehicle> g_vehicles;
std::map<int, int> g_modelWarmup; // modelId -> кадров загрузки прошло

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
        game::Resolve<RequestModelFn>(game::revc::kCStreamingRequestModel)(modelId, 0);
        return false;
    }
    if (it->second < kModelWarmupFrames) {
        game::Resolve<LoadAllModelsFn>(game::revc::kCStreamingLoadAllRequested)(false);
        ++it->second;
        return false;
    }
    return true;
}

// POD SEH-создание машины.
void* CreateVehicleRaw(void* pool, int model, float x, float y, float z, float heading) {
    VC_TRY {
        void* memory = game::Resolve<PoolNewFn>(game::revc::kCPoolVehicleNew)(pool);
        if (!memory) return nullptr;
        void* vehicle = game::Resolve<AutomobileCtorFn>(game::revc::kCAutomobileCtor)(
            memory, model, kMissionVehicle);
        if (!vehicle) return nullptr;
        auto* base = static_cast<char*>(vehicle);
        // Поза: позиция во встроенной матрице (pos = +0x38), SetHeading её
        // сохраняет; копию матрицы — в Rw-фрейм, иначе модель не там, где коллизия.
        *reinterpret_cast<float*>(base + 0x38) = x;
        *reinterpret_cast<float*>(base + 0x3C) = y;
        *reinterpret_cast<float*>(base + 0x40) = z;
        game::Resolve<SetHeadingFn>(game::revc::kCPlaceableSetHeading)(
            base + game::kPlaceableOffset, heading);
        void* frameMatrix =
            *reinterpret_cast<void**>(base + game::kFrameMatrixPtrOffset);
        if (frameMatrix) std::memcpy(frameMatrix, base + game::kEmbeddedMatrixOffset, 0x40);
        game::Resolve<UpdateRwFrameFn>(game::revc::kCEntityUpdateRwFrame)(vehicle);
        game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldAdd)(vehicle);
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
    void* vehicle = CreateVehicleRaw(pool, remote.model, remote.position.x,
                                       remote.position.y, remote.position.z, remote.heading);
    if (!vehicle) Log("vpool: создание машины не удалось (пул полон/битая модель/SEH)\n");
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
        }
        if (spawned.vehicle) continue;
        if (!EnsureModelReady(remote.model)) continue;
        spawned.vehicle = CreateVehicle(remote);
        if (!spawned.vehicle) continue;
        spawned.model = remote.model;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "vpool: машина id %u (модель %u) создана\n",
                      remote.id, remote.model);
        Log(buf);
    }
}

} // namespace vc::pools
