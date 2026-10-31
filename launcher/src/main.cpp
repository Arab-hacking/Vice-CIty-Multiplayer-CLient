// Лаунчер VC:MP, Этап 1: найти reVC.exe, запустить и заинжектить vc-client.dll.
//
// Поведение:
//   - путь к reVC.exe берётся из vc-launcher.ini рядом с лаунчером
//     ([Game] Path=...); если нет/невалиден — диалог выбора файла, путь сохраняется;
//   - vc-client.dll ищется рядом с лаунчером;
//   - игра стартует в CREATE_SUSPENDED, DLL инжектится классикой
//     VirtualAllocEx + WriteProcessMemory + CreateRemoteThread(LoadLibraryW),
//     затем процесс возобновляется.
// Сервер-браузер (список, ник, подключение) — следующие итерации лаунчера.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <string>

#include <shared/version.h>

namespace {

void ErrorBox(const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), L"VC:MP Launcher", MB_OK | MB_ICONERROR);
}

std::wstring ModuleDir(HMODULE module = nullptr) {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring p = path;
    const std::size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : p.substr(0, slash + 1);
}

bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring PickGameExe() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"reVC.exe\0reVC.exe\0Все файлы\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Выбери reVC.exe (целевой билд Vice City)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? std::wstring(file) : std::wstring();
}

bool InjectDll(HANDLE process, const std::wstring& dllPath, std::wstring& error) {
    const std::size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (!remote) {
        error = L"VirtualAllocEx не удался, код " + std::to_wstring(GetLastError());
        return false;
    }
    bool ok = WriteProcessMemory(process, remote, dllPath.c_str(), bytes, nullptr) != 0;
    HANDLE thread = nullptr;
    if (ok) {
        const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
        ok = thread != nullptr;
    }
    DWORD exitCode = 0;
    if (ok) {
        WaitForSingleObject(thread, 15000);
        GetExitCodeThread(thread, &exitCode);
        ok = exitCode != 0; // HMODULE загруженной DLL
        if (!ok) error = L"LoadLibraryW вернул NULL (DLL не загрузилась)";
    } else {
        error = L"WriteProcessMemory/CreateRemoteThread не удались, код " +
                std::to_wstring(GetLastError());
    }
    if (thread) CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return ok;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring launcherDir = ModuleDir();
    const std::wstring iniPath = launcherDir + L"vc-launcher.ini";
    const std::wstring dllPath = launcherDir + L"vc-client.dll";

    if (!FileExists(dllPath)) {
        ErrorBox(L"Рядом с лаунчером нет vc-client.dll:\n" + dllPath +
                 L"\n\nДержи vc-launcher.exe и vc-client.dll в одной папке.");
        return 1;
    }

    // Настройки подключения для клиента (DLL читает vc-client.ini рядом с собой).
    // Создаём с дефолтами, если игрок ещё не правил файл руками.
    const std::wstring clientIni = launcherDir + L"vc-client.ini";
    if (!FileExists(clientIni)) {
        WritePrivateProfileStringW(L"Server", L"IP", L"127.0.0.1", clientIni.c_str());
        WritePrivateProfileStringW(L"Server", L"Port", L"7777", clientIni.c_str());
        WritePrivateProfileStringW(L"Server", L"Nick", L"Player", clientIni.c_str());
    }

    wchar_t saved[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Game", L"Path", L"", saved, MAX_PATH, iniPath.c_str());
    std::wstring gamePath = saved;
    if (gamePath.empty() || !FileExists(gamePath)) {
        gamePath = PickGameExe();
        if (gamePath.empty()) return 0; // пользователь отменил
        WritePrivateProfileStringW(L"Game", L"Path", gamePath.c_str(), iniPath.c_str());
    }

    const std::wstring gameDir = gamePath.substr(0, gamePath.find_last_of(L"\\/") + 1);
    std::wstring cmdline = L"\"" + gamePath + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(gamePath.c_str(), cmdline.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, gameDir.c_str(), &si, &pi)) {
        ErrorBox(L"Не удалось запустить reVC.exe, код " + std::to_wstring(GetLastError()) +
                 L":\n" + gamePath);
        return 1;
    }

    std::wstring error;
    const bool injected = InjectDll(pi.hProcess, dllPath, error);
    ResumeThread(pi.hThread); // игру запускаем в любом случае
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (!injected) {
        ErrorBox(L"Игра запущена, но vc-client.dll не заинжектилась:\n" + error +
                 L"\n\nПопробуй запустить лаунчер от администратора.");
        return 1;
    }
    return 0;
}
