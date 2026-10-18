// Игровой сервер (заглушка Этапа 0).
//
// Дальше здесь будет: чтение server.cfg -> ENet host на порту 7777 ->
// загрузка Lua-gamemode -> тик-цикл: приём пакетов, пулы, стриминг и
// рассылка синхронизации. Расписание — docs/ROADMAP.md, Этапы 2-6.

#include <cstdio>

#include <shared/version.h>

int main() {
    std::printf("Vice City Multiplayer Server %s (protocol %u)\n",
                vc::CLIENT_VERSION, vc::PROTOCOL_VERSION);
    std::printf("Каркас проекта: сеть появится на Этапе 2, Lua-режимы — на Этапе 6.\n");
    return 0;
}
