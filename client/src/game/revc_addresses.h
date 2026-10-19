#pragma once

// АВТОГЕНЕРАЦИЯ: tools/gen_revc_addresses.py — НЕ редактировать руками.
// RVA символов целевого билда reVC.exe (x64, ASLR): реальный адрес =
// база модуля reVC.exe в процессе + RVA (см. vc::game::Resolve).
// reVC.exe SHA256: 5af4eb9122823c830b5d44ddfd7fabd2fa1f4db2fbd894e1240cef8e8450b3d0
// reVC.pdb RSDS GUID: 0d644e59-278c-4dfa-b9ec-596cc7494efd

#include <cstdint>

namespace vc::game::revc {

inline constexpr const char* kTargetExeSha256 = "5af4eb9122823c830b5d44ddfd7fabd2fa1f4db2fbd894e1240cef8e8450b3d0";
inline constexpr const char* kTargetPdbGuid = "0d644e59-278c-4dfa-b9ec-596cc7494efd";

// CPlayerPed* FindPlayerPed()
inline constexpr std::uintptr_t kFindPlayerPed = 0x0010EFB0u;

// CVehicle* FindPlayerVehicle()
inline constexpr std::uintptr_t kFindPlayerVehicle = 0x0010F060u;

// CVector FindPlayerCoors()
inline constexpr std::uintptr_t kFindPlayerCoors = 0x0010EE80u;

// CPlayerInfo* CWorld::Players[MAX_PLAYERS]
inline constexpr std::uintptr_t kCWorldPlayers = 0x00827A50u;

// void CWorld::Add(CEntity*)
inline constexpr std::uintptr_t kCWorldAdd = 0x0012AAA0u;

// void CWorld::Remove(CEntity*)
inline constexpr std::uintptr_t kCWorldRemove = 0x00133740u;

// bool CGame::Initialise(const char*)
inline constexpr std::uintptr_t kCGameInitialise = 0x00105220u;

// void CGame::Process() — кандидат на хук кадра
inline constexpr std::uintptr_t kCGameProcess = 0x001060E0u;

// uint8 CClock::ms_nGameClockHours
inline constexpr std::uintptr_t kCClockHours = 0x0061291Cu;

// uint8 CClock::ms_nGameClockMinutes
inline constexpr std::uintptr_t kCClockMinutes = 0x0061291Du;

// uint16 CClock::ms_nGameClockSeconds
inline constexpr std::uintptr_t kCClockSeconds = 0x00612920u;

// int16 CWeather::ForcedWeatherType
inline constexpr std::uintptr_t kCWeatherForcedType = 0x00E6C9ECu;

// int16 CWeather::NewWeatherType
inline constexpr std::uintptr_t kCWeatherNewType = 0x00E6C9E8u;

// int16 CWeather::OldWeatherType
inline constexpr std::uintptr_t kCWeatherOldType = 0x00E6C9E4u;

// CCamera TheCamera
inline constexpr std::uintptr_t kTheCamera = 0x0060D690u;

// void CStreaming::RequestModel(int32, int32)
inline constexpr std::uintptr_t kCStreamingRequestModel = 0x00126EA0u;

// void CStreaming::LoadAllRequestedModels(bool)
inline constexpr std::uintptr_t kCStreamingLoadAllRequested = 0x00123670u;

// CPool<CPed>* CPools::ms_pPedPool
inline constexpr std::uintptr_t kCPoolsPedPool = 0x0061ED70u;

// CPool<CVehicle>* CPools::ms_pVehiclePool
inline constexpr std::uintptr_t kCPoolsVehiclePool = 0x0061ED78u;

// CPool<CObject>* CPools::ms_pObjectPool
inline constexpr std::uintptr_t kCPoolsObjectPool = 0x0061ED90u;

// float CTimer::ms_fTimeStep
inline constexpr std::uintptr_t kCTimerTimeStep = 0x00826D44u;

// uint32& CTimer::GetTimeInMilliseconds()
inline constexpr std::uintptr_t kCTimerGetTimeMs = 0x0028AB10u;

// CAutomobile::CAutomobile(int32 model, uint8 createdBy)
inline constexpr std::uintptr_t kCAutomobileCtor = 0x002875B0u;

// CCivilianPed::CCivilianPed(ePedType, uint32 model)
inline constexpr std::uintptr_t kCCivilianPedCtor = 0x001CBAE0u;

// CPlayerPed::CPlayerPed()
inline constexpr std::uintptr_t kCPlayerPedCtor = 0x00214A40u;

// CMenuManager FrontEndMenuManager
inline constexpr std::uintptr_t kFrontEndMenuManager = 0x0061D900u;

// void CPed::Teleport(CVector) — заморозка игрока при вводе чата
inline constexpr std::uintptr_t kCPedTeleport = 0x001EB970u;

// void CPad::UpdatePads() — будущее отключение ввода
inline constexpr std::uintptr_t kCPadUpdatePads = 0x0010DA20u;

// CKeyboardState CPad::NewKeyState
inline constexpr std::uintptr_t kCPadNewKeyState = 0x0061E590u;

// void CKeyboardState::Clear()
inline constexpr std::uintptr_t kCKeyboardStateClear = 0x0010A7C0u;

// CPed* CPool<CPed>::New() — аллокация слота педа
inline constexpr std::uintptr_t kCPoolPedNew = 0x001D7B60u;

// CVehicle* CPool<CVehicle>::New() — аллокация слота транспорта
inline constexpr std::uintptr_t kCPoolVehicleNew = 0x002CA290u;

// float CEntity::GetDistanceFromCentreOfMassToBaseOfModel() — высота покоя машины при спавне
inline constexpr std::uintptr_t kCEntityDistToBase = 0x0014F7E0u;

// void CPed::ProcessControl() — хук отключает ИИ сетевых кукол
inline constexpr std::uintptr_t kCPedProcessControl = 0x001DCA20u;

// void CPlaceable::SetHeading(float) — на будущее
inline constexpr std::uintptr_t kCPlaceableSetHeading = 0x0010E810u;

// bool CSprite::CalcScreenCoors(in, out, w, h, check) — мир->экран для никнеймов
inline constexpr std::uintptr_t kCSpriteCalcScreenCoors = 0x0025ED00u;

// void CPed::SetMoveState(eMoveState) — анимация ходьбы/бега куклы (следующий шаг)
inline constexpr std::uintptr_t kCPedSetMoveState = 0x001E9E00u;

// void CEntity::UpdateRwFrame() — пересчёт матрицы Rw-клампа (без него модель не едет за позицией)
inline constexpr std::uintptr_t kCEntityUpdateRwFrame = 0x00151090u;

// void CPed::SetMoveAnim() — применяет анимацию по m_nMoveState (сам проверяет смену)
inline constexpr std::uintptr_t kCPedSetMoveAnim = 0x001E99D0u;

} // namespace vc::game::revc
