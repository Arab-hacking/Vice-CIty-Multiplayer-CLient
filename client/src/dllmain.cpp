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

#include <array>
#include <string>

#include <shared/version.h>

#include "core/sha256.h"
#include "game/hooks/hook_cgame_process.h"
#include "game/revc.h"
#include "network/net_client.h"
#include "render/overlay.h"

namespace {

HMODULE g_dllModule = nullptr;

// Папка, где лежит vc-client.dll (там же vc-client.ini с настройками сервера).
std::string DllDirectory() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_dllModule, path, MAX_PATH);
    std::string p = path;
    const std::size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
}

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

// SHA256 файла запущенного reVC.exe — сверяем с билдом, под который сделаны RVA.
std::string Sha256OfFile(const char* path) {
    HANDLE file = CreateFileA(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    vc::crypto::Sha256 hash;
    std::array<std::uint8_t, 65536> buffer{};
    DWORD read = 0;
    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) &&
           read > 0)
        hash.Update(buffer.data(), read);
    CloseHandle(file);
    const auto digest = hash.Final();
    char hex[65] = {};
    for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return hex;
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

    const std::string actualHash = Sha256OfFile(exePath);
    const bool buildMatches = actualHash == vc::game::revc::kTargetExeSha256;
    std::snprintf(buf, sizeof(buf), "  reVC.exe SHA256 (факт): %s -> %s\n",
                  actualHash.empty() ? "<не прочитать файл>" : actualHash.c_str(),
                  buildMatches ? "СОВПАЛ, адреса валидны" : "НЕ совпал, хуки ставить нельзя!");
    LogLine(log, buf);
    if (!buildMatches) {
        LogLine(log, "vc-client: пришлите этот reVC.exe + reVC.pdb для перегенерации адресов\n");
        if (log) std::fclose(log);
        return 0;
    }

    if (vc::game::hooks::InstallCGameProcessHook(log)) {
        LogLine(log, "vc-client: хук CGame::Process установлен, пишем координаты при движении\n");
        // Оверлей поверх D3D9 (панель ImGui, F7).
        if (!vc::render::InstallOverlayHooks(log))
            LogLine(log, "vc-client: оверлей не установился (см. выше), игра продолжается без него\n");
        // Сеть: подключение к серверу из vc-client.ini (Tick идёт из хука кадра).
        vc::network::NetClient::Instance().Init(log, DllDirectory() + "vc-client.ini");
        // Лог НЕ закрываем: хуки пишут в него из игрового потока всю сессию.
    } else {
        LogLine(log, "vc-client: хук поставить не удалось (см. выше)\n");
        if (log) std::fclose(log);
    }
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_dllModule = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        // Вежливо прощаемся с сервером (best-effort: без join потоков под loader lock).
        vc::network::NetClient::Instance().ShutdownBestEffort();
    }
    return TRUE;
}
