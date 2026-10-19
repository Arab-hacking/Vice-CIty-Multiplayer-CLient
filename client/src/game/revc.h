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

// Смещения полей, добытые дизассемблером из целевого reVC.exe (эта сборка!):
//  - CPlaceable-подобъект внутри CEntity/CPed: +0x08 (CMatrix там же);
//    методы CPlaceable (SetHeading) принимают this = ped+8, НЕ ped.
//  - CPed::m_fHealth = ped+0x454, m_fArmour = ped+0x458
//    (CPed::InflictDamage: armour+health сравнивается с уроном;
//     CPed::SetMoveAnim проверяет [ped+0x454] > 0 как «жив»).
inline constexpr std::uintptr_t kPlaceableOffset = 0x08;
inline constexpr std::uintptr_t kPedHealthOffset = 0x454;
inline constexpr std::uintptr_t kPedArmourOffset = 0x458;
// Встроенная матрица CPlaceable (pos на ped+0x38) и указатель на матрицу
// Rw-фрейма (CMatrix::pMatrix) — модель рисуется по второй, Teleport пишет
// только первую, поэтому после перемещения копируем 0x40 байт первой во вторую.
inline constexpr std::uintptr_t kEmbeddedMatrixOffset = 0x08;
inline constexpr std::uintptr_t kFrameMatrixPtrOffset = 0x48;

} // namespace vc::game
