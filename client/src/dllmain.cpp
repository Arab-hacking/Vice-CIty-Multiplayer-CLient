// Точка входа клиентской DLL под целевой билд reVC.exe (x64).
//
// Этап 1 (текущее состояние): проверить, что мы загружены именно в reVC.exe,
// вычислить базу модуля и разрешить через RVA из PDB адрес FindPlayerPed,
// записать диагностику в vc-client.log рядом с reVC.exe.
// Этап 1.5+: хук CGame::Process (RVA kCGameProcess) + D3D9-оверлей,
// Этап 3: спавн сетевых педов через CWorld::Add / конструкторы из revc_addresses.h.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>

#include <shared/version.h>

#include "game/revc.h"

namespace {

void LogLine(FILE* log, const char* text) {
    OutputDebugStringA(text);
    if (log) {
        std::fputs(text, log);
        std::fflush(log);
    }
}

bool IsHostReVC(char* pathBuf, DWORD pathLen) {
    if (GetModuleFileNameA(nullptr, pathBuf, pathLen) == 0) return false;
    const char* name = std::strrchr(pathBuf, '\\');
    name = name ? name + 1 : pathBuf;
    return _stricmp(name, "reVC.exe") == 0;
}

DWORD WINAPI InitThread(LPVOID) {
    char exePath[MAX_PATH] = {};
    if (!IsHostReVC(exePath, MAX_PATH)) {
        OutputDebugStringA("vc-client: хост-процесс не reVC.exe, инициализация пропущена\n");
        return 0;
    }

    // Лог рядом с reVC.exe.
    char logPath[MAX_PATH] = {};
    std::strncpy(logPath, exePath, MAX_PATH - 1);
    char* slash = std::strrchr(logPath, '\\');
    if (slash) slash[1] = '\0';
    else logPath[0] = '\0';
    std::strncat(logPath, "vc-client.log", MAX_PATH - std::strlen(logPath) - 1);
    FILE* log = std::fopen(logPath, "w");

    vc::game::g_moduleBase =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));

    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "vc-client %s: reVC.exe base = 0x%llX\n"
                  "  target exe SHA256 (ожидается): %s\n",
                  vc::CLIENT_VERSION,
                  static_cast<unsigned long long>(vc::game::g_moduleBase),
                  vc::game::revc::kTargetExeSha256);
    LogLine(log, buf);

    const auto findPlayerPed =
        vc::game::Resolve<vc::game::FindPlayerPedFn>(vc::game::revc::kFindPlayerPed);
    std::snprintf(buf, sizeof(buf),
                  "  FindPlayerPed @ 0x%llX (RVA 0x%llX)\n"
                  "  CGame::Process @ 0x%llX (RVA 0x%llX)\n",
                  static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(findPlayerPed)),
                  static_cast<unsigned long long>(vc::game::revc::kFindPlayerPed),
                  static_cast<unsigned long long>(
                      vc::game::g_moduleBase + vc::game::revc::kCGameProcess),
                  static_cast<unsigned long long>(vc::game::revc::kCGameProcess));
    LogLine(log, buf);

    // NOTE: вызывать FindPlayerPed здесь рано — игра ещё инициализируется.
    // Первый безопасный вызов — после загрузки мира, из хука CGame::Process.
    LogLine(log, "vc-client: OK, ожидание этапов хуков (см. docs/ROADMAP.md)\n");
    if (log) std::fclose(log);
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
