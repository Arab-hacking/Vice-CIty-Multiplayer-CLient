// Инлайн-хук CGame::Process (RVA 0x1060C0 в целевом билде reVC).
//
// Пролог функции в этом билде (проверено дизассемблером по reVC.exe):
//   48 89 5C 24 08    mov [rsp+8], rbx
//   57                push rdi
//   48 83 EC 30       sub rsp, 0x30
//   E8 xx xx xx xx    call rel32          <- позиционно-зависимая инструкция!
// Крадём 15 байт. В трамплине call пересобираем как абсолютный косвенный
// (FF 15 [rip+2]; EB 08; imm64), чтобы не зависеть от расстояния трамплина.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../core/seh_guard.h"
#include "../../network/net_client.h"
#include "../../pools/player_pool.h"
#include "../../pools/vehicle_pool.h"
#include "../../render/overlay.h"
#include "../revc.h"
#include "hook_cgame_process.h"

namespace vc::game::hooks {
namespace {

using CGameProcessFn = void (*)();
using FindPlayerCoorsFn = CVector (*)();

CGameProcessFn g_original = nullptr;
FILE* g_log = nullptr;
std::uint64_t g_frame = 0;

// Чтение HP педа (CPed::m_fHealth, смещение из дизассемблера) под SEH.
bool SafeReadPedHealth(void* ped, float* out) {
    VC_TRY {
        *out = *reinterpret_cast<float*>(static_cast<char*>(ped) + kPedHealthOffset);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// Машина локального игрока (FindPlayerVehicle) под SEH, или null.
void* SafeFindPlayerVehicle() {
    VC_TRY {
        auto find = Resolve<FindPlayerVehicleFn>(revc::kFindPlayerVehicle);
        return find ? find() : nullptr;
    } VC_EXCEPT {
        return nullptr;
    }
}

// Поза машины из встроенной матрицы: pos (+0x38) + базис (right/forward/up)
// + dword флагов. POD SEH-обёртка.
bool SafeReadVehiclePose(void* vehicle, float* pos, float* rows, unsigned int* flags) {
    VC_TRY {
        const auto* base = static_cast<const char*>(vehicle);
        const float* m = reinterpret_cast<const float*>(base + kEmbeddedMatrixOffset);
        rows[0] = m[0]; rows[1] = m[1]; rows[2] = m[2];
        rows[3] = m[4]; rows[4] = m[5]; rows[5] = m[6];
        rows[6] = m[8]; rows[7] = m[9]; rows[8] = m[10];
        pos[0] = m[12]; pos[1] = m[13]; pos[2] = m[14];
        *flags = *reinterpret_cast<const unsigned int*>(base + kEntityFlagsOffset);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// Место педа в машине: 0 — водитель (не в pPassengers, статус STATUS_PLAYER),
// 1..3 — пассажир pPassengers[seat-1], -1 — ещё не уселся (анимация входа).
int SafeFindSeat(void* vehicle, void* ped, unsigned int flags) {
    VC_TRY {
        const auto* base = static_cast<const char*>(vehicle);
        for (int i = 0; i < 3; ++i) {
            void* occupant = *reinterpret_cast<void* const*>(
                base + kVehiclePassengersOffset + i * sizeof(void*));
            if (occupant == ped) return i + 1;
        }
        return ((flags >> 3) & 0x1F) == 0 ? 0 : -1; // STATUS_PLAYER => за рулём
    } VC_EXCEPT {
        return -1;
    }
}

// Телепорт локального педа: Teleport + SetHeading (this=ped+8!) + перенос
// встроенной матрицы в матрицу Rw-фрейма + UpdateRwFrame. POD SEH-обёртка.
bool SafeTeleportLocalPed(void* ped, float x, float y, float z, float heading) {
    using TeleportFn = void (*)(void*, CVector);
    using SetHeadingFn = void (*)(void*, float);
    using UpdateRwFrameFn = void (*)(void*);
    VC_TRY {
        Resolve<TeleportFn>(revc::kCPedTeleport)(ped, CVector{x, y, z});
        auto* base = static_cast<char*>(ped);
        Resolve<SetHeadingFn>(revc::kCPlaceableSetHeading)(base + kPlaceableOffset, heading);
        void* frameMatrix = *reinterpret_cast<void**>(base + kFrameMatrixPtrOffset);
        if (frameMatrix) std::memcpy(frameMatrix, base + kEmbeddedMatrixOffset, 0x40);
        Resolve<UpdateRwFrameFn>(revc::kCEntityUpdateRwFrame)(ped);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

using SetObjectiveFn = void (*)(void*, int, void*);
using WarpPedIntoCarFn = void (*)(void*, void*);
constexpr int kObjectiveLeaveCar = 16;         // eObjective (Ped.h)
constexpr int kObjectiveEnterCarPassenger = 17;

// Посадка ЛОКАЛЬНОГО игрока пассажиром: в самой VC такой функции нет (Enter
// всегда сажает водителем) — берём миссионный путь SetObjective(17) +
// WarpPedIntoCar. Нюанс: WarpPedIntoCar для игрока ставит машине
// STATUS_PLAYER, из-за чего игра читала бы ввод пассажира как водительский;
// возвращаем STATUS_PHYSICS(3) — машину ведут сетевые позы.
bool SeatLocalPassengerRaw(void* ped, void* vehicle) {
    VC_TRY {
        Resolve<SetObjectiveFn>(revc::kCPedSetObjective)(ped, kObjectiveEnterCarPassenger,
                                                          vehicle);
        Resolve<WarpPedIntoCarFn>(revc::kCPedWarpPedIntoCar)(ped, vehicle);
        auto* flags = reinterpret_cast<unsigned int*>(static_cast<char*>(vehicle) +
                                                       kEntityFlagsOffset);
        *flags = (*flags & ~0xF8u) | (3u << 3); // STATUS_PHYSICS
        return true;
    } VC_EXCEPT {
        return false;
    }
}

// Выход пассажира: тот же OBJECTIVE_LEAVE_CAR, что игра сама ставит по
// клавише выхода (CPlayerInfo::Process) — анимацию и выписку из машины
// (RemovePassenger и т.д.) делает игра.
bool LeaveCarRaw(void* ped, void* vehicle) {
    VC_TRY {
        Resolve<SetObjectiveFn>(revc::kCPedSetObjective)(ped, kObjectiveLeaveCar, vehicle);
        return true;
    } VC_EXCEPT {
        return false;
    }
}

void OnFrameAfterOriginal() {
    ++g_frame;

    // Сеть живёт в своём потоке: сюда только складываем свежую позицию.
    auto findPed = Resolve<FindPlayerPedFn>(revc::kFindPlayerPed);
    auto findCoors = Resolve<FindPlayerCoorsFn>(revc::kFindPlayerCoors);
    void* ped = findPed ? findPed() : nullptr;
    std::optional<vc::Vector3> position;
    std::uint8_t health = 100;
    float rawHealth = 100.0f;
    if (ped) {
        const CVector p = findCoors();
        position = vc::Vector3{p.x, p.y, p.z};
        float hp = 100.0f;
        if (SafeReadPedHealth(ped, &hp) && hp >= 0.0f && hp <= 1000.0f) {
            rawHealth = hp;
            if (hp > 200.0f) hp = 200.0f; // VC: максимум с прокачкой ~200
            health = static_cast<std::uint8_t>(hp);
        }
    }

    // Детект смерти: HP было > 0, стало <= 0 (один раз на смерть).
    {
        static bool wasAlive = false;
        const bool alive = ped && rawHealth > 0.0f;
        if (wasAlive && ped && rawHealth <= 0.0f) {
            vc::network::NetClient::Instance().NotifyDeath();
            if (g_log) {
                std::fprintf(g_log, "[frame %llu] игрок погиб — уведомляем сервер\n",
                             static_cast<unsigned long long>(g_frame));
                std::fflush(g_log);
            }
        }
        if (ped) wasAlive = alive;
    }

    // «Мир готов» для телепорта от сервера: пед жив и позиция вменяема
    // 180 кадров подряд (не трогаем мир во время загрузки и экрана Wasted).
    static int stableFrames = 0;
    const bool sane = position && rawHealth > 0.0f && position->x > -5000.0f &&
                      position->x < 5000.0f && position->y > -5000.0f &&
                      position->y < 5000.0f && position->z > -500.0f && position->z < 5000.0f;
    stableFrames = sane ? stableFrames + 1 : 0;
    if (ped && stableFrames >= 180) {
        if (auto tp = vc::network::NetClient::Instance().TakePendingTeleport()) {
            const bool ok = SafeTeleportLocalPed(ped, tp->position.x, tp->position.y,
                                                 tp->position.z, tp->heading);
            if (g_log) {
                std::fprintf(g_log, "[frame %llu] телепорт сервера (%.1f, %.1f, %.1f) ok=%d\n",
                             static_cast<unsigned long long>(g_frame), tp->position.x,
                             tp->position.y, tp->position.z, ok ? 1 : 0);
                std::fflush(g_log);
            }
            const CVector p = findCoors();
            position = vc::Vector3{p.x, p.y, p.z};
        }
    }
    vc::network::NetClient::Instance().SetPlayerState(position, health);
    vc::pools::PlayerPool::Instance().Tick(); // куклы удалённых игроков
    vc::pools::VehiclePool::Instance().Tick(); // серверные машины

    // G — сесть пассажиром в ближайшую серверную машину / выйти из неё (как
    // в SA-MP; в самой VC посадки пассажиром нет). Фактическое место следом
    // определит блок ниже по pPassengers и сам доложит серверу.
    {
        static bool gWasDown = false;
        const bool gDown = (GetAsyncKeyState('G') & 0x8000) != 0;
        const bool pressed = gDown && !gWasDown;
        gWasDown = gDown;
        if (pressed && ped && rawHealth > 0.0f && !vc::render::IsChatOpen() &&
            !vc::render::IsOverlayVisible()) {
            auto& net = vc::network::NetClient::Instance();
            if (void* myVehicle = SafeFindPlayerVehicle()) {
                // Уже в машине: G высаживает только пассажира (водитель
                // выходит штатной клавишей игры).
                float vp[3] = {};
                float vr[9] = {};
                unsigned int vf = 0;
                if (SafeReadVehiclePose(myVehicle, vp, vr, &vf) &&
                    SafeFindSeat(myVehicle, ped, vf) > 0 && LeaveCarRaw(ped, myVehicle))
                    net.AddLocalChatLine("[G] выхожу из машины");
            } else if (position) {
                std::uint16_t nearId = 0;
                void* nearVeh = nullptr;
                if (vc::pools::VehiclePool::Instance().NearestServerVehicle(
                        *position, 8.0f, nearId, nearVeh)) {
                    if (!net.HasFreePassengerSeat(nearId)) {
                        net.AddLocalChatLine("[G] в ближайшей машине нет свободных пассажирских мест");
                    } else if (SeatLocalPassengerRaw(ped, nearVeh)) {
                        char buf[96];
                        std::snprintf(buf, sizeof(buf),
                                      "[G] сажусь пассажиром в машину id %u (G - выйти)",
                                      nearId);
                        net.AddLocalChatLine(buf);
                    }
                }
            }
        }
    }

    // Локальный игрок в СЕРВЕРНОЙ машине? Края посадки + поза (только водитель).
    {
        static std::uint16_t seatedId = 0;
        static int seatedSeat = -1;
        void* myVehicle = SafeFindPlayerVehicle();
        std::uint16_t serverId = 0;
        int seat = -1;
        float vpos[3] = {};
        float vrows[9] = {};
        if (myVehicle && ped) {
            serverId = vc::pools::VehiclePool::Instance().ServerIdOf(myVehicle);
            unsigned int vflags = 0;
            if (serverId != 0 && SafeReadVehiclePose(myVehicle, vpos, vrows, &vflags))
                seat = SafeFindSeat(myVehicle, ped, vflags);
        }
        auto& net = vc::network::NetClient::Instance();
        const bool seated = serverId != 0 && seat >= 0;
        if (seated && (serverId != seatedId || seat != seatedSeat)) {
            if (seatedId != 0) net.NotifyExitVehicle();
            net.NotifyEnterVehicle(serverId, static_cast<std::uint8_t>(seat));
            seatedId = serverId;
            seatedSeat = seat;
            if (g_log) {
                std::fprintf(g_log, "[frame %llu] в серверной машине id %u, место %d\n",
                             static_cast<unsigned long long>(g_frame), serverId, seat);
                std::fflush(g_log);
            }
        } else if (!seated && seatedId != 0) {
            net.NotifyExitVehicle();
            seatedId = 0;
            seatedSeat = -1;
            if (g_log) {
                std::fprintf(g_log, "[frame %llu] вышел из серверной машины\n",
                             static_cast<unsigned long long>(g_frame));
                std::fflush(g_log);
            }
        }
        if (seated && seat == 0) {
            const vc::Quaternion q = vc::QuatFromBasis(
                {vrows[0], vrows[1], vrows[2]}, {vrows[3], vrows[4], vrows[5]},
                {vrows[6], vrows[7], vrows[8]});
            net.SetLocalVehicleState(vc::network::NetClient::LocalVehicleState{
                serverId, vc::Vector3{vpos[0], vpos[1], vpos[2]}, q});
        } else {
            net.SetLocalVehicleState(std::nullopt);
        }
    }

    // Пока открыт ввод чата — держим педа на месте (игрок печатает, а не бегает на WASD).
    // Делается после оригинального CGame::Process: возвращаем педа в точку открытия чата.
    {
        static std::optional<CVector> frozenAt;
        if (vc::render::IsChatOpen() && ped) {
            auto findVehicle = Resolve<FindPlayerVehicleFn>(revc::kFindPlayerVehicle);
            const bool inVehicle = findVehicle && findVehicle() != nullptr;
            if (!inVehicle && position) {
                if (!frozenAt) frozenAt = CVector{position->x, position->y, position->z};
                using TeleportFn = void (*)(void*, CVector);
                Resolve<TeleportFn>(revc::kCPedTeleport)(ped, *frozenAt);
            }
        } else {
            frozenAt.reset();
        }
    }

    if (g_frame % 60 != 0 || !g_log) return; // лог — опрос ~раз в секунду при 60 FPS
    if (!ped) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::fprintf(g_log, "[frame %llu] игрок ещё не заспавнен\n",
                         static_cast<unsigned long long>(g_frame));
            std::fflush(g_log);
        }
        return;
    }
    // В лог — только если игрок заметно сдвинулся (иначе за сессию набегают мегабайты).
    static CVector lastLogged{1e9f, 1e9f, 1e9f};
    const CVector pos = findCoors();
    const float dx = pos.x - lastLogged.x, dy = pos.y - lastLogged.y, dz = pos.z - lastLogged.z;
    if (dx * dx + dy * dy + dz * dz > 0.25f) {
        lastLogged = pos;
        std::fprintf(g_log, "[frame %llu] ped=%p pos=(%.2f, %.2f, %.2f)\n",
                     static_cast<unsigned long long>(g_frame), ped, pos.x, pos.y, pos.z);
        std::fflush(g_log);
    }
}

void Hook_CGameProcess() {
    g_original();
    OnFrameAfterOriginal();
}

// Ожидаемые первые 15 байт пролога (смещение call не сверяем — оно зависит от линковки,
// но в одном билде постоянно; сверяем опкод и все остальные байты).
constexpr std::array<std::uint8_t, 10> kExpectedHead = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30};
constexpr std::size_t kCallOffset = 10;
constexpr std::size_t kStolenLen = 15;

void WriteAbsJump(std::uint8_t* dst, const void* target) {
    // FF 25 00 00 00 00 <imm64>
    dst[0] = 0xFF;
    dst[1] = 0x25;
    std::memset(dst + 2, 0, 4);
    const auto addr = reinterpret_cast<std::uint64_t>(target);
    std::memcpy(dst + 6, &addr, 8);
}

void SuspendOtherThreads(std::vector<HANDLE>& suspended) {
    const DWORD pid = GetCurrentProcessId();
    const DWORD myTid = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid && te.th32ThreadID != myTid) {
                HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (h) {
                    SuspendThread(h);
                    suspended.push_back(h);
                }
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

void ResumeThreads(std::vector<HANDLE>& suspended) {
    for (HANDLE h : suspended) {
        ResumeThread(h);
        CloseHandle(h);
    }
    suspended.clear();
}

} // namespace

std::uint64_t FrameCounter() { return g_frame; }

bool InstallCGameProcessHook(FILE* log) {
    g_log = log;
    auto* target = Resolve<std::uint8_t*>(revc::kCGameProcess);

    if (std::memcmp(target, kExpectedHead.data(), kExpectedHead.size()) != 0 ||
        target[kCallOffset] != 0xE8) {
        if (g_log) {
            char bytes[16 * 3 + 1] = {};
            for (int i = 0; i < 16; ++i)
                std::snprintf(bytes + i * 3, 4, "%02X ", target[i]);
            std::fprintf(g_log,
                         "hook: пролог CGame::Process не совпал с ожидаемым "
                         "(другой билд reVC?) — хук НЕ поставлен\n"
                         "hook: фактические байты: %s\n",
                         bytes);
            std::fflush(g_log);
        }
        return false;
    }

    // Адрес, куда ведёт украденный call rel32.
    std::int32_t rel = 0;
    std::memcpy(&rel, target + kCallOffset + 1, 4);
    auto* callTarget = target + kStolenLen + rel;

    // Трамплин: [10 байт головы][абсолютный call 14][абсолютный jmp назад 14]
    auto* trampoline = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, target, kExpectedHead.size());
    std::uint8_t* p = trampoline + kExpectedHead.size();
    // FF 15 02 00 00 00  EB 08  <imm64 target>
    p[0] = 0xFF; p[1] = 0x15; p[2] = 0x02; p[3] = 0; p[4] = 0; p[5] = 0;
    p[6] = 0xEB; p[7] = 0x08;
    const auto callAddr = reinterpret_cast<std::uint64_t>(callTarget);
    std::memcpy(p + 8, &callAddr, 8);
    WriteAbsJump(p + 16, target + kStolenLen);
    FlushInstructionCache(GetCurrentProcess(), trampoline, 64);
    g_original = reinterpret_cast<CGameProcessFn>(trampoline);

    // Патч начала функции — с приостановкой остальных потоков на всякий случай.
    std::vector<HANDLE> suspended;
    SuspendOtherThreads(suspended);
    DWORD oldProtect = 0;
    const bool ok = VirtualProtect(target, kStolenLen, PAGE_EXECUTE_READWRITE, &oldProtect) != 0;
    if (ok) {
        std::array<std::uint8_t, kStolenLen> patch{};
        patch.fill(0x90); // NOP на хвосте
        WriteAbsJump(patch.data(), reinterpret_cast<const void*>(&Hook_CGameProcess));
        std::memcpy(target, patch.data(), patch.size());
        DWORD tmp = 0;
        VirtualProtect(target, kStolenLen, oldProtect, &tmp);
        FlushInstructionCache(GetCurrentProcess(), target, kStolenLen);
    }
    ResumeThreads(suspended);

    if (g_log) {
        std::fprintf(g_log, "hook: CGame::Process -> трамплин %p, ok=%d\n",
                     static_cast<void*>(trampoline), ok ? 1 : 0);
        std::fflush(g_log);
    }
    return ok;
}

} // namespace vc::game::hooks
