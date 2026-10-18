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

#include "../../network/net_client.h"
#include "../revc.h"
#include "hook_cgame_process.h"

namespace vc::game::hooks {
namespace {

using CGameProcessFn = void (*)();
using FindPlayerCoorsFn = CVector (*)();

CGameProcessFn g_original = nullptr;
FILE* g_log = nullptr;
std::uint64_t g_frame = 0;

void OnFrameAfterOriginal() {
    ++g_frame;

    // Сеть крутим каждый кадр (главный поток): позиция, если игрок заспавнен.
    auto findPed = Resolve<FindPlayerPedFn>(revc::kFindPlayerPed);
    auto findCoors = Resolve<FindPlayerCoorsFn>(revc::kFindPlayerCoors);
    void* ped = findPed ? findPed() : nullptr;
    std::optional<vc::Vector3> position;
    if (ped) {
        const CVector p = findCoors();
        position = vc::Vector3{p.x, p.y, p.z};
    }
    vc::network::NetClient::Instance().Tick(GetTickCount64(), position);

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
