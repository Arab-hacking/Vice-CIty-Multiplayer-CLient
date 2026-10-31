// Серверные объекты и пикапы. Создание повторяет путь самой игры:
//   CPools::ms_pObjectPool->New()        -> слот в пуле объектов
//   CObject::CObject(model, true)        -> SetModelIndex (RW) + Init
//   поза: строки базиса + позиция во встроенной матрице (entity+0x08),
//         зануление скоростей CPhysical, копия матрицы в Rw-фрейм
//   CWorld::Add(entity)                  -> в мир
// Удаление: CWorld::Remove + scalar deleting destructor (vtable[0]).
// Поза переприменяется каждый кадр без Remove/Add (сектор мира не меняется),
// поэтому гравитация объекты не утаскивает. Всё под SEH: битая модель не
// должна ронять игру.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstring>
#include <map>

#include "../core/seh_guard.h"
#include "../game/revc.h"
#include "../network/net_client.h"
#include "object_pool.h"

namespace vc::pools {
namespace {

using PoolNewFn = void* (*)(void*);
using ObjectCtorFn = void* (*)(void*, int, bool);
using WorldAddRemoveFn = void (*)(void*);
using UpdateRwFrameFn = void (*)(void*);
using RequestModelFn = void (*)(int, int);
using LoadAllModelsFn = void (*)(bool);
using DeletingDtorFn = void* (*)(void*, unsigned int);
using FindPlayerCoorsFn = game::CVector (*)();

// Как у VehiclePool: DONT_REMOVE(1)|SCRIPTOWNED(2) - иначе модель выгрузят
// по LRU вместе с геометрией и объект «исчезнет».
constexpr int kStreamFlagsKeep = 3;
constexpr int kMaxCreateAttempts = 3;
constexpr long long kRetryDelayTicks = 300; // ~5 секунд между попытками
constexpr long long kPickupReportCooldownTicks = 150; // ~2.5 с между рапортами
constexpr float kPickupRadiusSq = 4.0f; // 2 м - дистанция подбора

FILE* g_log = nullptr;

struct SpawnedEntity {
    void* entity = nullptr;
    void* vtable = nullptr; // сверка «слот пула ещё наш»
    std::uint16_t model = 0;
    int attempts = 0;
    long long nextRetryTick = 0;
    long long spawnTick = 0;
    long long lastReportTick = -1000000; // только пикапы
};

std::map<std::uint16_t, SpawnedEntity> g_objects;
std::map<std::uint16_t, SpawnedEntity> g_pickups;
std::map<int, int> g_modelWarmup; // modelId -> кадров загрузки прошло
long long g_tickCount = 0;

void Log(const char* text) {
    OutputDebugStringA(text);
    if (g_log) {
        std::fputs(text, g_log);
        std::fflush(g_log);
    }
}

// CStreaming::HasModelLoaded(id) по массиву ms_aInfoForModel (как в исходниках).
bool ModelLoadedRaw(int modelId) {
    VC_TRY {
        const auto* info = game::Resolve<const char*>(game::revc::kCStreamingInfoArray);
        if (!info || modelId < 0) return false;
        const auto state = *reinterpret_cast<const std::uint8_t*>(
            info + modelId * game::kStreamingInfoStride + game::kStreamingLoadStateOffset);
        return state == game::kStreamStateLoaded;
    } VC_EXCEPT {
        return false;
    }
}

// True, когда модель запрошена И реально загружена (конструктор CObject на
// незагруженной модели падает точно так же, как CAutomobile).
bool EnsureModelReady(int modelId) {
    auto [it, inserted] = g_modelWarmup.try_emplace(modelId, 0);
    if (inserted) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "opool: запрашиваю модель %d\n", modelId);
        Log(buf);
        game::Resolve<RequestModelFn>(game::revc::kCStreamingRequestModel)(
            modelId, kStreamFlagsKeep);
    }
    if (ModelLoadedRaw(modelId)) {
        if (it->second >= 0) {
            char buf[80];
            std::snprintf(buf, sizeof(buf), "opool: модель %d загружена за %d кадров\n",
                          modelId, it->second);
            Log(buf);
            it->second = -1; // больше не логируем
        }
        return true;
    }
    game::Resolve<LoadAllModelsFn>(game::revc::kCStreamingLoadAllRequested)(false);
    if (it->second >= 0) ++it->second;
    return false;
}

// Углы Эйлера (градусы, R = Rz*Ry*Rx) -> строки базиса GTA (right/forward/up).
void EulerRows(float degX, float degY, float degZ, float rows[9]) {
    constexpr float kDegToRad = 0.017453292519943295f;
    const float sx = std::sin(degX * kDegToRad), cx = std::cos(degX * kDegToRad);
    const float sy = std::sin(degY * kDegToRad), cy = std::cos(degY * kDegToRad);
    const float sz = std::sin(degZ * kDegToRad), cz = std::cos(degZ * kDegToRad);
    rows[0] = cy * cz;                          // right
    rows[1] = cy * sz;
    rows[2] = -sy;
    rows[3] = sx * sy * cz - cx * sz;           // forward
    rows[4] = sx * sy * sz + cx * cz;
    rows[5] = sx * cy;
    rows[6] = cx * sy * cz + sx * sz;           // up
    rows[7] = cx * sy * sz - sx * cz;
    rows[8] = cx * cy;
}

// Запись позы без Remove/Add: матрица + нулевые скорости + копия в Rw-фрейм.
bool WritePoseRaw(void* entity, const float rows[9], float x, float y, float z) {
    VC_TRY {
        auto* base = static_cast<char*>(entity);
        float* m = reinterpret_cast<float*>(base + game::kEmbeddedMatrixOffset);
        m[0] = rows[0]; m[1] = rows[1]; m[2] = rows[2];   // right   (+0x08)
        m[4] = rows[3]; m[5] = rows[4]; m[6] = rows[5];   // forward (+0x18)
        m[8] = rows[6]; m[9] = rows[7]; m[10] = rows[8];  // up      (+0x28)
        m[12] = x; m[13] = y; m[14] = z;                  // pos     (+0x38)
        std::memset(base + game::kPhysicalMoveSpeedOffset, 0, 24); // move+turn
        void* frameMatrix = *reinterpret_cast<void**>(base + game::kFrameMatrixPtrOffset);
        if (frameMatrix) std::memcpy(frameMatrix, base + game::kEmbeddedMatrixOffset, 0x40);
        game::Resolve<UpdateRwFrameFn>(game::revc::kCEntityUpdateRwFrame)(entity);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// Слот пула всё ещё «наш»? Игра могла удалить объект и отдать слот другому.
bool StillOursRaw(void* entity, void* vtable, std::uint16_t model) {
    VC_TRY {
        const auto* base = static_cast<const char*>(entity);
        void* nowVtable = *reinterpret_cast<void* const*>(base);
        const auto nowModel = *reinterpret_cast<const std::uint16_t*>(base + 0x6C);
        return nowVtable == vtable && nowModel == model;
    } VC_EXCEPT {
        return false;
    }
}

void* ReadVtableRaw(void* entity) {
    VC_TRY {
        return *reinterpret_cast<void**>(entity);
    } VC_EXCEPT {
        return nullptr;
    }
}

// POD SEH-создание объекта. *failStage: 1 = пул полон, 2 = конструктор упал.
void* CreateObjectRaw(void* pool, int model, float x, float y, float z,
                      const float rows[9], int* failStage) {
    *failStage = 2;
    VC_TRY {
        void* memory = game::Resolve<PoolNewFn>(game::revc::kCPoolObjectNew)(pool);
        if (!memory) {
            *failStage = 1;
            return nullptr;
        }
        void* entity = game::Resolve<ObjectCtorFn>(game::revc::kCObjectCtor)(
            memory, model, true);
        if (!entity) return nullptr;
        WritePoseRaw(entity, rows, x, y, z);
        game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldAdd)(entity);
        *failStage = 0;
        return entity;
    } VC_EXCEPT {
        return nullptr;
    }
}

void DestroyRaw(void* entity) {
    VC_TRY {
        game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldRemove)(entity);
        void** vtable = *reinterpret_cast<void***>(entity);
        reinterpret_cast<DeletingDtorFn>(vtable[0])(entity, 1);
    } VC_EXCEPT {
    }
}

// Безопасное удаление: только если слот пула всё ещё наш.
void DestroySafe(SpawnedEntity& spawned, std::uint16_t id, const char* what) {
    if (!spawned.entity) return;
    if (StillOursRaw(spawned.entity, spawned.vtable, spawned.model)) {
        DestroyRaw(spawned.entity);
    } else {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "opool: %s id %u уже удалён игрой - пропускаю деструктор\n", what, id);
        Log(buf);
    }
    spawned.entity = nullptr;
    spawned.vtable = nullptr;
}

// Позиция локального игрока (FindPlayerCoors). False - ещё не в мире.
bool LocalPlayerPosRaw(float out[3]) {
    VC_TRY {
        auto findCoors = game::Resolve<FindPlayerCoorsFn>(game::revc::kFindPlayerCoors);
        if (!findCoors) return false;
        const game::CVector coors = findCoors();
        out[0] = coors.x;
        out[1] = coors.y;
        out[2] = coors.z;
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// Общий путь спавна объекта/пикапа: модель, лимит попыток, проверка «наш ли».
// True - сущность создана и posable в этом кадре.
bool EnsureSpawned(SpawnedEntity& spawned, std::uint16_t id, std::uint16_t model,
                   const vc::Vector3& pos, const float rows[9], const char* what) {
    if (spawned.entity && spawned.model != model) {
        DestroySafe(spawned, id, what);
        spawned.attempts = 0;
    }
    if (spawned.entity) {
        if (g_tickCount % 30 == 0) {
            if (!StillOursRaw(spawned.entity, spawned.vtable, spawned.model)) {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "opool: игра удалила %s id %u через ~%lld кадров\n", what, id,
                              g_tickCount - spawned.spawnTick);
                Log(buf);
                spawned.entity = nullptr;
                spawned.vtable = nullptr;
                spawned.attempts = kMaxCreateAttempts; // не пересоздавать в цикле
                return false;
            }
            // Модель держим загруженной, как VehiclePool.
            game::Resolve<RequestModelFn>(game::revc::kCStreamingRequestModel)(
                model, kStreamFlagsKeep);
        }
        return true;
    }
    if (spawned.attempts >= kMaxCreateAttempts) return false;
    if (g_tickCount < spawned.nextRetryTick) return false;
    if (!EnsureModelReady(model)) return false;

    auto** poolPtr = game::Resolve<void**>(game::revc::kCPoolsObjectPool);
    void* pool = poolPtr ? *poolPtr : nullptr;
    if (!pool) {
        Log("opool: ms_pObjectPool == nullptr (мир ещё не готов)\n");
        return false;
    }
    int failStage = 0;
    void* entity = CreateObjectRaw(pool, model, pos.x, pos.y, pos.z, rows, &failStage);
    if (!entity) {
        ++spawned.attempts;
        spawned.nextRetryTick = g_tickCount + kRetryDelayTicks;
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      failStage == 1 ? "opool: пул объектов полон (модель %u)\n"
                                     : "opool: конструктор CObject упал на модели %u\n",
                      model);
        Log(buf);
        return false;
    }
    spawned.entity = entity;
    spawned.vtable = ReadVtableRaw(entity);
    spawned.model = model;
    spawned.spawnTick = g_tickCount;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "opool: %s id %u (модель %u) создан\n", what, id,
                  model);
    Log(buf);
    return true;
}

template <typename Remote>
void EraseUnwanted(std::map<std::uint16_t, SpawnedEntity>& map,
                   const std::vector<Remote>& remotes, const char* what) {
    for (auto it = map.begin(); it != map.end();) {
        bool wanted = false;
        for (const auto& remote : remotes)
            if (remote.id == it->first) {
                wanted = true;
                break;
            }
        if (!wanted) {
            if (it->second.entity) {
                DestroySafe(it->second, it->first, what);
                char buf[96];
                std::snprintf(buf, sizeof(buf), "opool: %s id %u удалён\n", what,
                              it->first);
                Log(buf);
            }
            it = map.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

ObjectPool& ObjectPool::Instance() {
    static ObjectPool instance;
    return instance;
}

void ObjectPool::Init(FILE* log) { g_log = log; }

void ObjectPool::Tick() {
    ++g_tickCount;
    auto& net = vc::network::NetClient::Instance();
    const auto objects = net.RemoteObjects();
    const auto pickups = net.RemotePickups();

    EraseUnwanted(g_objects, objects, "объект");
    EraseUnwanted(g_pickups, pickups, "пикап");
    if (objects.empty() && pickups.empty()) return;

    // Спавнить можно только когда локальный игрок уже в мире.
    auto findPed = game::Resolve<game::FindPlayerPedFn>(game::revc::kFindPlayerPed);
    if (!findPed || !findPed()) return;

    // Статичные объекты: поза сервера переприменяется каждый кадр.
    for (const auto& remote : objects) {
        SpawnedEntity& spawned = g_objects[remote.id];
        float rows[9];
        EulerRows(remote.rotX, remote.rotY, remote.rotZ, rows);
        if (EnsureSpawned(spawned, remote.id, remote.model, remote.position, rows,
                          "объект"))
            WritePoseRaw(spawned.entity, rows, remote.position.x, remote.position.y,
                         remote.position.z);
    }

    // Пикапы: вращение вокруг Z + лёгкое парение, подбор по близости.
    float localPos[3] = {0.0f, 0.0f, 0.0f};
    const bool haveLocalPos = LocalPlayerPosRaw(localPos);
    const float t = static_cast<float>(GetTickCount64()) * 0.001f;
    for (const auto& remote : pickups) {
        SpawnedEntity& spawned = g_pickups[remote.id];
        const float rzDeg = std::fmod(t * 120.0f, 360.0f); // ~120 градусов/с
        const float bobZ = remote.position.z + 0.3f + 0.15f * std::sin(t * 2.5f);
        float rows[9];
        EulerRows(0.0f, 0.0f, rzDeg, rows);
        if (!EnsureSpawned(spawned, remote.id, remote.model,
                           {remote.position.x, remote.position.y, bobZ}, rows, "пикап"))
            continue;
        WritePoseRaw(spawned.entity, rows, remote.position.x, remote.position.y, bobZ);

        if (!haveLocalPos) continue;
        const float dx = remote.position.x - localPos[0];
        const float dy = remote.position.y - localPos[1];
        const float dz = remote.position.z - localPos[2];
        if (dx * dx + dy * dy + dz * dz < kPickupRadiusSq &&
            g_tickCount - spawned.lastReportTick > kPickupReportCooldownTicks) {
            spawned.lastReportTick = g_tickCount;
            net.ReportPickup(remote.id);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "Вы подобрали пикап id %u (модель %u)",
                          remote.id, remote.model);
            net.AddLocalChatLine(buf);
            std::snprintf(buf, sizeof(buf), "opool: подбор пикапа id %u доложен\n",
                          remote.id);
            Log(buf);
        }
    }
}

} // namespace vc::pools
