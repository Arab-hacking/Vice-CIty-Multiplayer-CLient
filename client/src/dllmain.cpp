// Точка входа клиентской DLL (Этап 1 — proof of concept, пока заглушка).
//
// План развития файла:
//   DllMain(DLL_PROCESS_ATTACH)
//     -> создать поток инициализации (НЕ делать тяжёлую работу прямо в DllMain!)
//     -> дождаться загрузки игры
//     -> поставить хуки (главный цикл, Direct3D8)
//     -> создать CClient и подключиться к серверу из параметров лаунчера

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shared/version.h>

namespace {

DWORD WINAPI InitThread(LPVOID) {
    // TODO (Этап 1): ожидание готовности игры, установка хуков, CClient.
    OutputDebugStringA("vc-client: DLL загружена в процесс игры, версия " vc::CLIENT_VERSION "\n");
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
