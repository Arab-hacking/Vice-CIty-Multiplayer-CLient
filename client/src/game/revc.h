#pragma once

#include <cstdint>

#include "revc_addresses.h"

// Доступ к внутренностям целевого reVC.exe через RVA из его PDB.
// reVC.exe собран с ASLR, поэтому адрес = база главного модуля процесса + RVA.
namespace vc::game {

// Базу модуля выставляет dllmain при инициализации (адрес reVC.exe в процессе).
inline std::uintptr_t g_moduleBase = 0;

template <typename T>
[[nodiscard]] inline T Resolve(std::uintptr_t rva) {
    return reinterpret_cast<T>(g_moduleBase + rva);
}

// Типы ключевых функций reVC (x64, стандартный MSVC ABI).
// Структуры CPlayerPed/CVehicle будут описаны по PDB-типам на следующих этапах.
struct CVector {
    float x, y, z;
};

using FindPlayerPedFn = void* (*)();
using FindPlayerVehicleFn = void* (*)();

} // namespace vc::game
