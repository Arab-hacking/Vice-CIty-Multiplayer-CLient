// Сетевые педы-куклы. Создание педа в reVC повторяет путь самой игры:
//   CPools::ms_pPedPool->New()  -> слот в пуле педов
//   CCivilianPed::CCivilianPed(PEDTYPE_CIVMALE=4, model=skin)
//   CPed::Teleport(pos)         -> позиция
//   CWorld::Add(ped)            -> в мир
// Удаление: CWorld::Remove + виртуальный scalar deleting destructor (vtable[0]).
// Модели скинов запрашиваются через CStreaming и грузятся LoadAllRequestedModels
// несколько кадров до спавна. Поворот — CPlaceable::SetHeading к целевому углу,
// анимация — CPed::SetMoveState (STILL/WALK/RUN по скорости сглаживания).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>

#include <MinHook.h>

#include "../game/revc.h"
#include "../network/net_client.h"
#include "player_pool.h"

namespace vc::pools {
namespace {

using PoolNewFn = void* (*)(void*);
using CivilianCtorFn = void* (*)(void*, int, unsigned int);
using TeleportFn = void (*)(void*, game::CVector);
using SetHeadingFn = void (*)(void*, float);
using SetMoveStateFn = void (*)(void*, int);
using WorldAddRemoveFn = void (*)(void*);
using RequestModelFn = void (*)(int, int);
using LoadAllModelsFn = void (*)(bool);
using ProcessControlFn = void (*)(void*);
using DeletingDtorFn = void* (*)(void*, unsigned int);

constexpr int kPedTypeCivMale = 4; // ePedType из reVC
constexpr int kModelWarmupFrames = 30;
// eMoveState в reVC: NONE=0, STILL=1, WALK=2, RUN=3, SPRINT=4 (визуально, без риска).
constexpr int kMoveStill = 1;
constexpr int kMoveWalk = 2;
constexpr int kMoveRun = 3;
constexpr float kPi = 3.14159265f;

FILE* g_log = nullptr;
ProcessControlFn g_origProcessControl = nullptr;

struct Puppet {
    void* ped = nullptr;
    std::uint16_t skin = 7;
    std::string nick;
    vc::Vector3 current{};
    float heading = 0.0f;
};

std::map<std::uint16_t, Puppet> g_puppets;
std::unordered_set<const void*> g_puppetPeds;
std::map<int, int> g_modelWarmup; // modelId -> кадров загрузки прошло

void Log(const std::string& text) {
    OutputDebugStringA(text.c_str());
    if (g_log) {
        std::fputs(text.c_str(), g_log);
        std::fflush(g_log);
    }
}

void HookProcessControl(void* self) {
    if (g_puppetPeds.contains(self)) return; // у куклы нет своего ИИ
    g_origProcessControl(self);
}

// True, когда модель запрошена и ей дали время загрузиться.
bool EnsureModelReady(int modelId) {
    auto [it, inserted] = g_modelWarmup.try_emplace(modelId, 0);
    if (inserted) {
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

void* CreatePuppet(int skin, const vc::Vector3& position, float heading) {
    auto** poolPtr = game::Resolve<void**>(game::revc::kCPoolsPedPool);
    void* pool = poolPtr ? *poolPtr : nullptr;
    if (!pool) {
        Log("pool: ms_pPedPool == nullptr (мир ещё не готов)\n");
        return nullptr;
    }
    void* memory = game::Resolve<PoolNewFn>(game::revc::kCPoolPedNew)(pool);
    if (!memory) {
        Log("pool: CPool<CPed>::New() вернул nullptr (пул полон)\n");
        return nullptr;
    }
    void* ped = game::Resolve<CivilianCtorFn>(game::revc::kCCivilianPedCtor)(
        memory, kPedTypeCivMale, static_cast<unsigned int>(skin));
    if (!ped) return nullptr;
    game::Resolve<TeleportFn>(game::revc::kCPedTeleport)(
        ped, game::CVector{position.x, position.y, position.z});
    game::Resolve<SetHeadingFn>(game::revc::kCPlaceableSetHeading)(ped, heading);
    game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldAdd)(ped);
    return ped;
}

void DestroyPuppet(void* ped) {
    game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldRemove)(ped);
    void** vtable = *reinterpret_cast<void***>(ped);
    reinterpret_cast<DeletingDtorFn>(vtable[0])(ped, 1); // деструктор + возврат слота в пул
}

// Кратчайший доворот угла.
float LerpAngle(float from, float to, float t) {
    float diff = to - from;
    while (diff > kPi) diff -= 2.0f * kPi;
    while (diff < -kPi) diff += 2.0f * kPi;
    return from + diff * t;
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
        if (puppet.ped) out.push_back({puppet.nick, puppet.current});
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

    // Удаление вышедших/вышедших из стрима.
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

    // Спавнить можно только когда локальный игрок уже в мире.
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
        }

        if (!puppet.ped) {
            // Позиция (0,0,0) — игрок только подключился, sync ещё не приходил.
            const vc::Vector3& tp = remote.targetPosition;
            if (tp.x * tp.x + tp.y * tp.y + tp.z * tp.z < 0.01f) continue;
            if (!EnsureModelReady(remote.skin)) continue;
            puppet.ped = CreatePuppet(remote.skin, tp, remote.heading);
            if (!puppet.ped) continue;
            puppet.skin = remote.skin;
            puppet.current = tp;
            puppet.heading = remote.heading;
            g_puppetPeds.insert(puppet.ped);
            Log("pool: кукла создана для " + remote.nick + " (skin " +
                std::to_string(remote.skin) + ")\n");
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
        puppet.heading = LerpAngle(puppet.heading, remote.heading, 0.25f);

        game::Resolve<TeleportFn>(game::revc::kCPedTeleport)(
            puppet.ped, game::CVector{puppet.current.x, puppet.current.y, puppet.current.z});
        game::Resolve<SetHeadingFn>(game::revc::kCPlaceableSetHeading)(puppet.ped,
                                                                        puppet.heading);
        // Анимация по скорости (~60 fps): >0.09 м/кадр — бег, >0.015 — ходьба.
        const int moveState =
            frameSpeed > 0.09f ? kMoveRun : (frameSpeed > 0.015f ? kMoveWalk : kMoveStill);
        game::Resolve<SetMoveStateFn>(game::revc::kCPedSetMoveState)(puppet.ped, moveState);
    }
}

} // namespace vc::pools
