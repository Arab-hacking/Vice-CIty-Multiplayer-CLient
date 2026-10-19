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

#include "../game/revc.h"
#include "../network/net_client.h"
#include "player_pool.h"

namespace vc::pools {
namespace {

using PoolNewFn = void* (*)(void*);
using CivilianCtorFn = void* (*)(void*, int, unsigned int);
using TeleportFn = void (*)(void*, game::CVector);
using WorldAddRemoveFn = void (*)(void*);
using RequestModelFn = void (*)(int, int);
using LoadAllModelsFn = void (*)(bool);
using ProcessControlFn = void (*)(void*);
using DeletingDtorFn = void* (*)(void*, unsigned int);

constexpr int kPedTypeCivMale = 4;   // ePedType из reVC
constexpr int kPuppetModelId = 7;    // male01 в peds.ide Vice City
constexpr int kModelWarmupFrames = 30;

FILE* g_log = nullptr;
ProcessControlFn g_origProcessControl = nullptr;

struct Puppet {
    void* ped = nullptr;
    vc::Vector3 current{};
};

std::map<std::uint16_t, Puppet> g_puppets;
std::unordered_set<const void*> g_puppetPeds;
bool g_modelRequested = false;
int g_modelWarmup = 0;

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

bool EnsureModelLoading() {
    if (!g_modelRequested) {
        game::Resolve<RequestModelFn>(game::revc::kCStreamingRequestModel)(kPuppetModelId, 0);
        g_modelRequested = true;
        return false;
    }
    if (g_modelWarmup < kModelWarmupFrames) {
        game::Resolve<LoadAllModelsFn>(game::revc::kCStreamingLoadAllRequested)(false);
        ++g_modelWarmup;
        return false;
    }
    return true;
}

void* CreatePuppet(const vc::Vector3& position) {
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
        memory, kPedTypeCivMale, static_cast<unsigned int>(kPuppetModelId));
    if (!ped) return nullptr;
    game::Resolve<TeleportFn>(game::revc::kCPedTeleport)(ped, game::CVector{position.x, position.y, position.z});
    game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldAdd)(ped);
    return ped;
}

void DestroyPuppet(void* ped) {
    game::Resolve<WorldAddRemoveFn>(game::revc::kCWorldRemove)(ped);
    void** vtable = *reinterpret_cast<void***>(ped);
    reinterpret_cast<DeletingDtorFn>(vtable[0])(ped, 1); // деструктор + возврат слота в пул
}

} // namespace

PlayerPool& PlayerPool::Instance() {
    static PlayerPool instance;
    return instance;
}

bool PlayerPool::IsPuppet(const void* ped) const { return g_puppetPeds.contains(ped); }

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
    const bool modelReady = EnsureModelLoading();

    for (const auto& remote : remotes) {
        if (!remote.hasPosition) continue;
        Puppet& puppet = g_puppets[remote.id];
        if (!puppet.ped) {
            // Позиция (0,0,0) — игрок только подключился, sync ещё не приходил.
            const vc::Vector3& tp = remote.targetPosition;
            if (tp.x * tp.x + tp.y * tp.y + tp.z * tp.z < 0.01f) continue;
            if (!modelReady) continue;
            puppet.ped = CreatePuppet(remote.targetPosition);
            if (!puppet.ped) continue;
            puppet.current = remote.targetPosition;
            g_puppetPeds.insert(puppet.ped);
            Log(("pool: кукла создана для " + remote.nick + "\n").c_str());
            continue;
        }
        // Плавное движение к целевой точке; на больших дистанциях — телепорт.
        const vc::Vector3& target = remote.targetPosition;
        const float dx = target.x - puppet.current.x;
        const float dy = target.y - puppet.current.y;
        const float dz = target.z - puppet.current.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > 30.0f * 30.0f) {
            puppet.current = target;
        } else {
            constexpr float kSmooth = 0.35f;
            puppet.current.x += dx * kSmooth;
            puppet.current.y += dy * kSmooth;
            puppet.current.z += dz * kSmooth;
        }
        game::Resolve<TeleportFn>(game::revc::kCPedTeleport)(
            puppet.ped, game::CVector{puppet.current.x, puppet.current.y, puppet.current.z});

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
