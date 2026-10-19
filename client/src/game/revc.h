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
// Раскладка CEntity/CVehicle этой сборки (подтверждено исходниками reVC и
// дизассемблером): флаги+тип+статус — один dword на +0x60 (тип биты 0-2,
// статус биты 3-7, bUsesCollision бит 8, bIsStatic бит 10), m_modelIndex
// int16 на +0x6C, CVehicle::m_nDoorLock на +0x2D8 (CARLOCK_UNLOCKED=1).
inline constexpr std::uintptr_t kEntityFlagsOffset = 0x60;
inline constexpr std::uintptr_t kEntityModelIndexOffset = 0x6C;
inline constexpr std::uintptr_t kVehicleDoorLockOffset = 0x2D8;
// CStreamingInfo: шаг массива 0x20, m_loadState на +0x10 (STREAMSTATE_LOADED=1),
// m_flags на +0x11. HasModelLoaded(id) == (loadState == 1).
inline constexpr std::uintptr_t kStreamingInfoStride = 0x20;
inline constexpr std::uintptr_t kStreamingLoadStateOffset = 0x10;
inline constexpr std::uint8_t kStreamStateLoaded = 1;

} // namespace vc::game
