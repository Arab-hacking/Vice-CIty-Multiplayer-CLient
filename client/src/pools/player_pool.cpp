// Сетевые педы-куклы. Создание педа в reVC повторяет путь самой игры:
//   CPools::ms_pPedPool->New()  -> слот в пуле педов
//   CCivilianPed::CCivilianPed(PEDTYPE_CIVMALE=4, model=7)
//   CPed::Teleport(pos)         -> позиция
//   CWorld::Add(ped)            -> в мир
// Удаление: CWorld::Remove + виртуальный scalar deleting destructor (vtable[0]).
// Модель 7 (male01 из peds.ide) запрашивается через CStreaming и грузится
// LoadAllRequestedModels несколько кадров до первого спавна.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>

#include <MinHook.h>

#include "../core/seh_guard.h"
#include "../game/revc.h"
#include "../network/net_client.h"
#include "player_pool.h"
#include "vehicle_pool.h"

namespace vc::pools {
namespace {

using PoolNewFn = void* (*)(void*);
using CivilianCtorFn = void* (*)(void*, int, unsigned int);
using TeleportFn = void (*)(void*, game::CVector);
using UpdateRwFrameFn = void (*)(void*);
using SetHeadingFn = void (*)(void*, float);
using SetMoveStateFn = void (*)(void*, int);
using SetMoveAnimFn = void (*)(void*);

// eMoveState в reVC: NONE=0, STILL=1, WALK=2, RUN=3, SPRINT=4.
constexpr int kMoveStill = 1;
constexpr int kMoveWalk = 2;
constexpr int kMoveRun = 3;
constexpr float kPi = 3.14159265f;

// Кратчайший доворот угла.
float LerpAngle(float from, float to, float t) {
    float diff = to - from;
    while (diff > kPi) diff -= 2.0f * kPi;
    while (diff < -kPi) diff += 2.0f * kPi;
    return from + diff * t;
}

// Перемещение куклы (POD SEH-обёртка вокруг сырых оффсетов).
// Факты из дизассемблера reVC.exe этой сборки:
//  - CPed::Teleport пишет позицию только во ВСТРОЕННУЮ матрицу CPlaceable
//    (CMatrix по смещению ped+0x08, pos = ped+0x38) — тень/физика её видят;
//  - матрица Rw-фрейма клампа (по которой рисуется МОДЕЛЬ) — отдельная копия,
//    указатель на неё CMatrix::pMatrix лежит по смещению ped+0x48
//    (CMatrix::Attach при спавне копирует фрейм->встроенную, обратно — никто);
//  - CEntity::UpdateRwFrame лишь помечает фрейм грязным (Frame::updateObjects),
//    LTM пересчитается при рендере из матрицы ФРЕЙМА.
// Поэтому: Teleport + копируем встроенную матрицу (64 байта) в матрицу фрейма
// + UpdateRwFrame. Без копирования модель навсегда стоит на точке спавна.
bool MovePuppetRaw(void* ped, float x, float y, float z, float heading, int moveState) {
    VC_TRY {
        game::Resolve<TeleportFn>(game::revc::kCPedTeleport)(ped, game::CVector{x, y, z});
        auto* base = static_cast<char*>(ped);
        // CPlaceable::SetHeading принимает this = ped+8 (подобъект), НЕ ped!
        game::Resolve<SetHeadingFn>(game::revc::kCPlaceableSetHeading)(
            base + game::kPlaceableOffset, heading);
        void* frameMatrix =
            *reinterpret_cast<void**>(base + game::kFrameMatrixPtrOffset);
        if (frameMatrix) std::memcpy(frameMatrix, base + game::kEmbeddedMatrixOffset, 0x40);
        game::Resolve<UpdateRwFrameFn>(game::revc::kCEntityUpdateRwFrame)(ped);
        // Анимация ног: SetMoveState пишет поле, SetMoveAnim применяет анимацию
        // (сам проверяет, сменилось ли состояние — можно звать каждый кадр).
        game::Resolve<SetMoveStateFn>(game::revc::kCPedSetMoveState)(ped, moveState);
        game::Resolve<SetMoveAnimFn>(game::revc::kCPedSetMoveAnim)(ped);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

void MovePuppet(void* ped, const vc::Vector3& position, float heading, int moveState) {
    MovePuppetRaw(ped, position.x, position.y, position.z, heading, moveState);
}
using WorldAddRemoveFn = void (*)(void*);
using RequestModelFn = void (*)(int, int);
using LoadAllModelsFn = void (*)(bool);
using ProcessControlFn = void (*)(void*);
using DeletingDtorFn = void* (*)(void*, unsigned int);

constexpr int kPedTypeCivMale = 4;   // ePedType из reVC


FILE* g_log = nullptr;
ProcessControlFn g_origProcessControl = nullptr;

struct Puppet {
    void* ped = nullptr;
    std::uint16_t skin = 7;
    std::string nick;
    vc::Vector3 current{};
    float heading = 0.0f;
    std::uint8_t health = 100;
    std::uint16_t seatedVehicleId = 0; // серверная машина, куда кукла посажена
};

std::map<std::uint16_t, Puppet> g_puppets;
std::unordered_set<const void*> g_puppetPeds;
std::map<int, int> g_modelWarmup; // modelId -> кадров загрузки прошло

void Log(const char* text) {
    OutputDebugStringA(text);
    if (g_log) {
        std::fputs(text, g_log);
        std::fflush(g_log);
    }
}

void HookProcessControl(void* self) {
    if (g_puppetPeds.contains(self)) return; // у куклы нет своего ИИ
    g_origProcessControl(self);
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

// True, когда модель скина запрошена И реально загружена (игра ждёт именно так).
bool EnsureModelReady(int modelId) {
    auto [it, inserted] = g_modelWarmup.try_emplace(modelId, 0);
    if (inserted) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "pool: запрашиваю модель %d\n", modelId);
        Log(buf);
        // Флаги 3 = DONT_REMOVE|SCRIPTOWNED: иначе стриминг выгрузит модель
        // редкого скина и кукла станет невидимой (см. vehicle_pool.cpp).
        game::Resolve<RequestModelFn>(game::revc::kCStreamingRequestModel)(modelId, 3);
    }
    if (ModelLoadedRaw(modelId)) return true;
    game::Resolve<LoadAllModelsFn>(game::revc::kCStreamingLoadAllRequested)(false);
    ++it->second;
    return false;
}

// POD SEH-создание педа: битый skin id не должен ронять игру.
void* CreatePuppetRaw(void* pool, int skin, float x, float y, float z) {
    VC_TRY {
        void* memory = game::Resolve<PoolNewFn>(game::revc::kCPoolPedNew)(pool);
        if (!memory) return nullptr;
        void* ped = game::Resolve<CivilianCtorFn>(game::revc::kCCivilianPedCtor)(
            memory, kPedTypeCivMale, static_cast<unsigned int>(skin));
        if (!ped) return nullptr;
        MovePuppet(ped, vc::Vector3{x, y, z}, 0.0f, kMoveStill);
        game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldAdd)(ped);
        return ped;
    } VC_EXCEPT {
        return nullptr;
    }
}

void* CreatePuppet(const vc::Vector3& position, int skin) {
    auto** poolPtr = game::Resolve<void**>(game::revc::kCPoolsPedPool);
    void* pool = poolPtr ? *poolPtr : nullptr;
    if (!pool) {
        Log("pool: ms_pPedPool == nullptr (мир ещё не готов)\n");
        return nullptr;
    }
    void* ped = CreatePuppetRaw(pool, skin, position.x, position.y, position.z);
    if (!ped) Log("pool: создание педа не удалось (пул полон/битый скин/исключение)\n");
    return ped;
}

void DestroyPuppet(void* ped) {
    game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldRemove)(ped);
    void** vtable = *reinterpret_cast<void***>(ped);
    reinterpret_cast<DeletingDtorFn>(vtable[0])(ped, 1); // деструктор + возврат слота в пул
}

// Посадка куклы в машину каноничным путём скриптового WARP_CHAR_INTO_CAR:
// SetObjective(ENTER_CAR_AS_DRIVER=18 / AS_PASSENGER=17) + WarpPedIntoCar.
// Дальше педа в салоне позиционирует сама игра (CWorld::Process ->
// SetPedPositionInCar) — наш хук ProcessControl этому не мешает.
// Высадка — уничтожением и пересозданием куклы: деструктор CPed сам
// аккуратно снимает педа из машины (pDriver=nil / RemovePassenger).
using SetObjectiveFn = void (*)(void*, int, void*);
using WarpPedIntoCarFn = void (*)(void*, void*);
constexpr int kObjectiveEnterCarPassenger = 17;
constexpr int kObjectiveEnterCarDriver = 18;

bool SeatPuppetRaw(void* ped, void* vehicle, bool driver) {
    VC_TRY {
        game::Resolve<SetObjectiveFn>(game::revc::kCPedSetObjective)(
            ped, driver ? kObjectiveEnterCarDriver : kObjectiveEnterCarPassenger, vehicle);
        game::Resolve<WarpPedIntoCarFn>(game::revc::kCPedWarpPedIntoCar)(ped, vehicle);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

} // namespace

PlayerPool& PlayerPool::Instance() {
    static PlayerPool instance;
    return instance;
}

bool PlayerPool::IsPuppet(const void* ped) const { return g_puppetPeds.contains(ped); }

std::vector<PuppetRenderInfo> PlayerPool::RenderInfos() const {
    std::vector<PuppetRenderInfo> out;
    for (const auto& [id, puppet] : g_puppets)
        if (puppet.ped) out.push_back({puppet.nick, puppet.current, puppet.health});
    return out;
}

void PlayerPool::Init(FILE* log) {
    g_log = log;
    // MinHook мог уже инициализировать оверлей — ALREADY_INITIALIZED тоже ок.
    const MH_STATUS initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        Log("pool: MH_Initialize не удался, ИИ кукол не отключится\n");
        return;
    }
    void* processControl = game::Resolve<void*>(game::revc::kCPedProcessControl);
    if (MH_CreateHook(processControl, reinterpret_cast<void*>(&HookProcessControl),
                      reinterpret_cast<void**>(&g_origProcessControl)) == MH_OK) {
        MH_EnableHook(processControl);
        Log("pool: хук CPed::ProcessControl установлен (ИИ кукол выключен)\n");
    } else {
        Log("pool: не удалось поставить хук CPed::ProcessControl\n");
    }
}

void PlayerPool::Tick() {
    const auto remotes = network::NetClient::Instance().RemotePlayers();

    // Удаление вышедших.
    for (auto it = g_puppets.begin(); it != g_puppets.end();) {
        const bool stillOnline =
            std::any_of(remotes.begin(), remotes.end(),
                        [&](const auto& r) { return r.id == it->first; });
        if (!stillOnline) {
            if (it->second.ped) {
                DestroyPuppet(it->second.ped);
                g_puppetPeds.erase(it->second.ped);
                Log("pool: кукла удалена\n");
            }
            it = g_puppets.erase(it);
        } else {
            ++it;
        }
    }
    if (remotes.empty()) return;

    // Спавнить можно только когда локальный игрок уже в мире и модель загружена.
    auto findPed = game::Resolve<game::FindPlayerPedFn>(game::revc::kFindPlayerPed);
    if (!findPed || !findPed()) return;

    for (const auto& remote : remotes) {
        if (!remote.hasPosition) continue;
        Puppet& puppet = g_puppets[remote.id];
        puppet.nick = remote.nick;
        // Смена скина: пересоздаём куклу с новой моделью.
        if (puppet.ped && puppet.skin != remote.skin) {
            DestroyPuppet(puppet.ped);
            g_puppetPeds.erase(puppet.ped);
            puppet.ped = nullptr;
            puppet.seatedVehicleId = 0;
        }
        if (!puppet.ped) {
            // Позиция (0,0,0) — игрок только подключился, sync ещё не приходил.
            const vc::Vector3& tp = remote.targetPosition;
            if (tp.x * tp.x + tp.y * tp.y + tp.z * tp.z < 0.01f) continue;
            if (!EnsureModelReady(remote.skin)) continue;
            puppet.ped = CreatePuppet(remote.targetPosition, remote.skin);
            if (!puppet.ped) continue;
            puppet.skin = remote.skin;
            puppet.current = remote.targetPosition;
            g_puppetPeds.insert(puppet.ped);
            Log(("pool: кукла создана для " + remote.nick + "\n").c_str());
            continue;
        }
        puppet.health = remote.health;

        // Посадка/высадка: сервер сообщает через VehicleOccupant, в какой
        // машине сидит игрок. Посаженную куклу ведёт сама игра — мы её не
        // телепортируем. Высадка = пересоздание педа (деструктор чистит салон).
        if (remote.inVehicleId != puppet.seatedVehicleId) {
            if (puppet.seatedVehicleId != 0) {
                DestroyPuppet(puppet.ped);
                g_puppetPeds.erase(puppet.ped);
                puppet.ped = nullptr;
                puppet.seatedVehicleId = 0;
                Log(("pool: кукла " + remote.nick + " высажена (пересоздана)\n").c_str());
                continue; // пересоздастся на следующем кадре уже пешком
            }
            void* vehicle = VehiclePool::Instance().GameVehicleOf(remote.inVehicleId);
            if (vehicle && SeatPuppetRaw(puppet.ped, vehicle, remote.seat == 0)) {
                puppet.seatedVehicleId = remote.inVehicleId;
                Log(("pool: кукла " + remote.nick + " посажена в машину\n").c_str());
            }
        }
        if (puppet.seatedVehicleId != 0) {
            // Машину могли удалить из-под куклы (стрим-аут): пересаживаем на ноги.
            if (!VehiclePool::Instance().GameVehicleOf(puppet.seatedVehicleId)) {
                DestroyPuppet(puppet.ped);
                g_puppetPeds.erase(puppet.ped);
                puppet.ped = nullptr;
                puppet.seatedVehicleId = 0;
            }
            puppet.current = remote.targetPosition; // для никнейма над машиной
            continue;
        }

        // Плавное движение к целевой точке; на больших дистанциях — телепорт.
        const vc::Vector3& target = remote.targetPosition;
        const float dx = target.x - puppet.current.x;
        const float dy = target.y - puppet.current.y;
        const float dz = target.z - puppet.current.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        float frameSpeed = 0.0f;
        if (distSq > 30.0f * 30.0f) {
            puppet.current = target;
        } else {
            constexpr float kSmooth = 0.35f;
            puppet.current.x += dx * kSmooth;
            puppet.current.y += dy * kSmooth;
            puppet.current.z += dz * kSmooth;
            frameSpeed = std::sqrt(distSq) * kSmooth; // метры за кадр
        }
        // Поворот по направлению движения: вперёд = (-sin h, cos h).
        const float flatDistSq = dx * dx + dy * dy;
        if (flatDistSq > 0.04f * 0.04f) {
            const float desired = std::atan2(-dx, dy);
            puppet.heading = LerpAngle(puppet.heading, desired, 0.25f);
        }
        // Анимация по скорости (~60 fps): >0.09 м/кадр — бег, >0.015 — ходьба.
        const int moveState =
            frameSpeed > 0.09f ? kMoveRun : (frameSpeed > 0.015f ? kMoveWalk : kMoveStill);
        MovePuppet(puppet.ped, puppet.current, puppet.heading, moveState);

        // Диагностика (0.6.3): раз в ~5 c печатаем цель/текущую/счётчик синков,
        // чтобы понять, где рвётся движение куклы.
        static std::uint64_t diagFrame = 0;
        if (g_log && (++diagFrame % 300 == 0)) {
            std::fprintf(g_log,
                         "pool[diag]: %s target=(%.1f, %.1f, %.1f) current=(%.1f, %.1f, %.1f) "
                         "syncs=%u\n",
                         remote.nick.c_str(), target.x, target.y, target.z, puppet.current.x,
                         puppet.current.y, puppet.current.z, remote.syncCount);
            std::fflush(g_log);
        }
    }
}

} // namespace vc::pools
