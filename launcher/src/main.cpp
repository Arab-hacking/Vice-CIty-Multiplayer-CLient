// Лаунчер игрока (заглушка Этапа 0/1).
//
// Будущие обязанности main_window/injector/server_browser:
//   1) показать список серверов (master-server + избранное), пинг/онлайн;
//   2) проверить, что найдена GTA VC с exe 1.0 US;
//   3) запустить gta-vc.exe в CREATE_SUSPENDED, заинжектить vc-client.dll,
//      передать IP:порт и ник, возобновить процесс.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shared/version.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    MessageBoxA(nullptr,
                "Vice City Multiplayer Launcher " vc::CLIENT_VERSION
                "\n\nКаркас проекта. Лаунчер появится на Этапе 1 (см. docs/ROADMAP.md).",
                "VC:MP Launcher", MB_OK | MB_ICONINFORMATION);
    return 0;
}
