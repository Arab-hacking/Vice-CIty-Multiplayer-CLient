#pragma once

#include <cstdint>
#include <cstdio>

// Инлайн-хук CGame::Process() целевого билда reVC (Этап 1).
// Реализация вручную под КОНКРЕТНЫЙ пролог функции из PDB-билда,
// без MinHook: байты пролога сверяются перед патчем, при несовпадении
// хук не ставится (защита от чужого билда reVC.exe).
namespace vc::game::hooks {

// log — открытый vc-client.log; хук пишет туда координаты игрока при движении.
bool InstallCGameProcessHook(FILE* log);

// Сколько кадров CGame::Process прошло через хук (для оверлея/диагностики).
std::uint64_t FrameCounter();

} // namespace vc::game::hooks
