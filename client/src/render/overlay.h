#pragma once

#include <cstdio>

// ImGui-оверлей поверх Direct3D9 (reVC рисует через RW D3D9 backend).
// Хуки EndScene/Reset ставятся через MinHook по адресам из vtable
// временного dummy-устройства. Панель показывается/прячется клавишей F7.
namespace vc::render {

bool InstallOverlayHooks(FILE* log);

} // namespace vc::render
