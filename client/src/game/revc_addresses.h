#pragma once

// АВТОГЕНЕРАЦИЯ: tools/gen_revc_addresses.py — НЕ редактировать руками.
// RVA символов целевого билда reVC.exe (x64, ASLR): реальный адрес =
// база модуля reVC.exe в процессе + RVA (см. vc::game::Resolve).
// reVC.exe SHA256: 92b762b1e9839f35c78f58f13e03319d33ccca87e54ef4178d7e3d3c6b40fee6
// reVC.pdb RSDS GUID: e0273426-08e9-4cc1-8628-05c06279b1c7

#include <cstdint>

namespace vc::game::revc {

inline constexpr const char* kTargetExeSha256 = "92b762b1e9839f35c78f58f13e03319d33ccca87e54ef4178d7e3d3c6b40fee6";
inline constexpr const char* kTargetPdbGuid = "e0273426-08e9-4cc1-8628-05c06279b1c7";

// CPlayerPed* FindPlayerPed()
inline constexpr std::uintptr_t kFindPlayerPed = 0x001101C0u;

// CVehicle* FindPlayerVehicle()
inline constexpr std::uintptr_t kFindPlayerVehicle = 0x00110270u;

// CVector FindPlayerCoors()
inline constexpr std::uintptr_t kFindPlayerCoors = 0x00110090u;

// CPlayerInfo* CWorld::Players[MAX_PLAYERS]
inline constexpr std::uintptr_t kCWorldPlayers = 0x0083CB30u;

// void CWorld::Add(CEntity*)
inline constexpr std::uintptr_t kCWorldAdd = 0x0012D210u;

// void CWorld::Remove(CEntity*)
inline constexpr std::uintptr_t kCWorldRemove = 0x00135920u;

// bool CGame::Initialise(const char*)
inline constexpr std::uintptr_t kCGameInitialise = 0x00105FE0u;

// void CGame::Process() — кандидат на хук кадра
inline constexpr std::uintptr_t kCGameProcess = 0x00106F10u;

// uint8 CClock::ms_nGameClockHours
inline constexpr std::uintptr_t kCClockHours = 0x006279ACu;

// uint8 CClock::ms_nGameClockMinutes
inline constexpr std::uintptr_t kCClockMinutes = 0x006279ADu;

// uint16 CClock::ms_nGameClockSeconds
inline constexpr std::uintptr_t kCClockSeconds = 0x006279B0u;

// int16 CWeather::ForcedWeatherType
inline constexpr std::uintptr_t kCWeatherForcedType = 0x00F7AF8Cu;

// int16 CWeather::NewWeatherType
inline constexpr std::uintptr_t kCWeatherNewType = 0x00F7AF88u;

// int16 CWeather::OldWeatherType
inline constexpr std::uintptr_t kCWeatherOldType = 0x00F7AF84u;

// CCamera TheCamera
inline constexpr std::uintptr_t kTheCamera = 0x00622720u;

// void CStreaming::RequestModel(int32, int32)
inline constexpr std::uintptr_t kCStreamingRequestModel = 0x00128F20u;

// void CStreaming::LoadAllRequestedModels(bool)
inline constexpr std::uintptr_t kCStreamingLoadAllRequested = 0x00125170u;

// CPool<CPed>* CPools::ms_pPedPool
inline constexpr std::uintptr_t kCPoolsPedPool = 0x00633E00u;

// CPool<CVehicle>* CPools::ms_pVehiclePool
inline constexpr std::uintptr_t kCPoolsVehiclePool = 0x00633E08u;

// CPool<CObject>* CPools::ms_pObjectPool
inline constexpr std::uintptr_t kCPoolsObjectPool = 0x00633E20u;

// float CTimer::ms_fTimeStep
inline constexpr std::uintptr_t kCTimerTimeStep = 0x0083BDD4u;

// uint32& CTimer::GetTimeInMilliseconds()
inline constexpr std::uintptr_t kCTimerGetTimeMs = 0x0029B1E0u;

// CAutomobile::CAutomobile(int32 model, uint8 createdBy)
inline constexpr std::uintptr_t kCAutomobileCtor = 0x00297D00u;

// CCivilianPed::CCivilianPed(ePedType, uint32 model)
inline constexpr std::uintptr_t kCCivilianPedCtor = 0x001DB2F0u;

// CPlayerPed::CPlayerPed()
inline constexpr std::uintptr_t kCPlayerPedCtor = 0x00224370u;

// CMenuManager FrontEndMenuManager
inline constexpr std::uintptr_t kFrontEndMenuManager = 0x00632990u;

// void CPed::Teleport(CVector) — заморозка игрока при вводе чата
inline constexpr std::uintptr_t kCPedTeleport = 0x001FB240u;

// void CPad::UpdatePads() — будущее отключение ввода
inline constexpr std::uintptr_t kCPadUpdatePads = 0x0010EC30u;

// CKeyboardState CPad::NewKeyState
inline constexpr std::uintptr_t kCPadNewKeyState = 0x00633620u;

// void CKeyboardState::Clear()
inline constexpr std::uintptr_t kCKeyboardStateClear = 0x0010B5C0u;

// CPed* CPool<CPed>::New() — аллокация слота педа
inline constexpr std::uintptr_t kCPoolPedNew = 0x001E7340u;

// CVehicle* CPool<CVehicle>::New() — аллокация слота транспорта
inline constexpr std::uintptr_t kCPoolVehicleNew = 0x002DA9F0u;

// float CEntity::GetDistanceFromCentreOfMassToBaseOfModel() — высота покоя машины при спавне
inline constexpr std::uintptr_t kCEntityDistToBase = 0x00150D20u;

// CStreamingInfo CStreaming::ms_aInfoForModel[] — m_loadState по +0x10, шаг 0x20 (HasModelLoaded)
inline constexpr std::uintptr_t kCStreamingInfoArray = 0x0063A030u;

// void CPed::SetObjective(eObjective, void*) — перед WarpPedIntoCar (ENTER_CAR_AS_DRIVER=18/PASSENGER=17)
inline constexpr std::uintptr_t kCPedSetObjective = 0x00210350u;

// void CPed::WarpPedIntoCar(CVehicle*) — мгновенная посадка педа в машину (как скриптовый WARP_CHAR_INTO_CAR)
inline constexpr std::uintptr_t kCPedWarpPedIntoCar = 0x001FCCF0u;

// int CPed::GiveWeapon(eWeaponType, uint32, bool) — выдача оружия педу (модель в руку вешает сама игра)
inline constexpr std::uintptr_t kCPedGiveWeapon = 0x001E59E0u;

// void CPed::SetCurrentWeapon(eWeaponType) — взять оружие в руки
inline constexpr std::uintptr_t kCPedSetCurrentWeapon = 0x001F3310u;

// void CPed::ProcessControl() — хук отключает ИИ сетевых кукол
inline constexpr std::uintptr_t kCPedProcessControl = 0x001EC120u;

// void CPlaceable::SetHeading(float) — на будущее
inline constexpr std::uintptr_t kCPlaceableSetHeading = 0x0010FA20u;

// bool CSprite::CalcScreenCoors(in, out, w, h, check) — мир->экран для никнеймов
inline constexpr std::uintptr_t kCSpriteCalcScreenCoors = 0x0026E7A0u;

// void CPed::SetMoveState(eMoveState) — анимация ходьбы/бега куклы (следующий шаг)
inline constexpr std::uintptr_t kCPedSetMoveState = 0x001F96D0u;

// void CEntity::UpdateRwFrame() — пересчёт матрицы Rw-клампа (без него модель не едет за позицией)
inline constexpr std::uintptr_t kCEntityUpdateRwFrame = 0x001524F0u;

// void CPed::SetMoveAnim() — применяет анимацию по m_nMoveState (сам проверяет смену)
inline constexpr std::uintptr_t kCPedSetMoveAnim = 0x001F92A0u;

// CObject::CObject(int32 model, bool createRW) — конструктор серверного объекта/пикапа
inline constexpr std::uintptr_t kCObjectCtor = 0x001D2730u;

// CPool<CObject>::New() — слот пула объектов
inline constexpr std::uintptr_t kCPoolObjectNew = 0x001D3510u;

} // namespace vc::game::revc
