// Точка входа клиентской DLL под целевой билд reVC.exe (x64), Этап 1.
//
// Поток инициализации: проверить хост (reVC.exe) -> открыть vc-client.log
// рядом с игрой -> база модуля + резолв RVA из PDB -> поставить хук
// CGame::Process, который раз в ~секунду пишет в лог координаты игрока
// (FindPlayerPed/FindPlayerCoors из PDB-адресов). Оверлей — шагом позже.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>

#include <shared/version.h>

#include "game/hooks/hook_cgame_process.h"
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

    char logPath[MAX_PATH] = {};
    std::strncpy(logPath, exePath, MAX_PATH - 1);
    char* slash = std::strrchr(logPath, '\\');
    if (slash) slash[1] = '\0';
    else logPath[0] = '\0';
    std::strncat(logPath, "vc-client.log", MAX_PATH - std::strlen(logPath) - 1);
    FILE* log = std::fopen(logPath, "w");

    vc::game::g_moduleBase =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));

    char buf[640];
    std::snprintf(buf, sizeof(buf),
                  "vc-client %s: reVC.exe base = 0x%llX\n"
                  "  target exe SHA256 (ожидается): %s\n"
                  "  FindPlayerPed RVA 0x%llX, CGame::Process RVA 0x%llX\n",
                  vc::CLIENT_VERSION,
                  static_cast<unsigned long long>(vc::game::g_moduleBase),
                  vc::game::revc::kTargetExeSha256,
                  static_cast<unsigned long long>(vc::game::revc::kFindPlayerPed),
                  static_cast<unsigned long long>(vc::game::revc::kCGameProcess));
    LogLine(log, buf);

    if (vc::game::hooks::InstallCGameProcessHook(log)) {
        LogLine(log, "vc-client: хук CGame::Process установлен, пишем координаты раз в ~секунду\n");
        // Лог НЕ закрываем: хук пишет в него из игрового потока всю сессию.
    } else {
        LogLine(log, "vc-client: хук поставить не удалось (см. выше)\n");
        if (log) std::fclose(log);
    }
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
