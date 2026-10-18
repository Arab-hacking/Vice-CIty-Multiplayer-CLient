// Лаунчер игрока (заглушка).
//
// Будущие обязанности main_window/injector/server_browser:
//   1) показать список серверов (master-server + избранное), пинг/онлайн
//      через RakNet offline-ping из shared/net (как vc-netprobe);
//   2) проверить, что найден именно целевой reVC.exe (SHA256 из docs/REVC-BUILD.md);
//   3) запустить reVC.exe в CREATE_SUSPENDED, заинжектить vc-client.dll (x64),
//      передать IP:порт и ник, возобновить процесс.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

#include <shared/version.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::string text =
        std::string("Vice City Multiplayer Launcher ") + vc::CLIENT_VERSION +
        "\n\nКаркас проекта. Лаунчер (сервер-браузер + инжектор в reVC x64) "
        "появится на Этапе 1 (см. docs/ROADMAP.md).";
    MessageBoxA(nullptr, text.c_str(), "VC:MP Launcher", MB_OK | MB_ICONINFORMATION);
    return 0;
}
