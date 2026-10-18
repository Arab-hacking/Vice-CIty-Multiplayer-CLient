#pragma once

#include <cstdio>

// Инлайн-хук CGame::Process() целевого билда reVC (Этап 1).
// Реализация вручную под КОНКРЕТНЫЙ пролог функции из PDB-билда,
// без MinHook: байты пролога сверяются перед патчем, при несовпадении
// хук не ставится (защита от чужого билда reVC.exe).
namespace vc::game::hooks {

// log — открытый vc-client.log; хук пишет туда координаты игрока ~1 раз/сек.
bool InstallCGameProcessHook(FILE* log);

} // namespace vc::game::hooks
