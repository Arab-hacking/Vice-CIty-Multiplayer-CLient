#pragma once

// АВТОГЕНЕРАЦИЯ: tools/gen_revc_addresses.py — НЕ редактировать руками.
// RVA символов целевого билда reVC.exe (x64, ASLR): реальный адрес =
// база модуля reVC.exe в процессе + RVA (см. vc::game::Resolve).
// reVC.exe SHA256: 0211d1f0c6418cd0b388ad3539adaa87310285b3043b301d444da8bdd290c019
// reVC.pdb RSDS GUID: b51b658e-0f6e-45c9-b450-54fbac0cce01

#include <cstdint>

namespace vc::game::revc {

inline constexpr const char* kTargetExeSha256 = "0211d1f0c6418cd0b388ad3539adaa87310285b3043b301d444da8bdd290c019";
inline constexpr const char* kTargetPdbGuid = "b51b658e-0f6e-45c9-b450-54fbac0cce01";

// CPlayerPed* FindPlayerPed()
inline constexpr std::uintptr_t kFindPlayerPed = 0x00110EB0u;

// CVehicle* FindPlayerVehicle()
inline constexpr std::uintptr_t kFindPlayerVehicle = 0x00110F60u;

// CVector FindPlayerCoors()
inline constexpr std::uintptr_t kFindPlayerCoors = 0x00110D80u;

// CPlayerInfo* CWorld::Players[MAX_PLAYERS]
inline constexpr std::uintptr_t kCWorldPlayers = 0x0082E7A0u;

// void CWorld::Add(CEntity*)
inline constexpr std::uintptr_t kCWorldAdd = 0x0012E280u;

// void CWorld::Remove(CEntity*)
inline constexpr std::uintptr_t kCWorldRemove = 0x00136990u;

// bool CGame::Initialise(const char*)
inline constexpr std::uintptr_t kCGameInitialise = 0x00106CF0u;

// void CGame::Process() — кандидат на хук кадра
inline constexpr std::uintptr_t kCGameProcess = 0x00107C20u;

// uint8 CClock::ms_nGameClockHours
inline constexpr std::uintptr_t kCClockHours = 0x0061961Cu;

// uint8 CClock::ms_nGameClockMinutes
inline constexpr std::uintptr_t kCClockMinutes = 0x0061961Du;

// uint16 CClock::ms_nGameClockSeconds
inline constexpr std::uintptr_t kCClockSeconds = 0x00619620u;

// int16 CWeather::ForcedWeatherType
inline constexpr std::uintptr_t kCWeatherForcedType = 0x00F60C2Cu;

// int16 CWeather::NewWeatherType
inline constexpr std::uintptr_t kCWeatherNewType = 0x00F60C28u;

// int16 CWeather::OldWeatherType
inline constexpr std::uintptr_t kCWeatherOldType = 0x00F60C24u;

// CCamera TheCamera
inline constexpr std::uintptr_t kTheCamera = 0x00614390u;

// void CStreaming::RequestModel(int32, int32)
inline constexpr std::uintptr_t kCStreamingRequestModel = 0x00129CF0u;

// void CStreaming::LoadAllRequestedModels(bool)
inline constexpr std::uintptr_t kCStreamingLoadAllRequested = 0x00125F40u;

// CPool<CPed>* CPools::ms_pPedPool
inline constexpr std::uintptr_t kCPoolsPedPool = 0x00625A70u;

// CPool<CVehicle>* CPools::ms_pVehiclePool
inline constexpr std::uintptr_t kCPoolsVehiclePool = 0x00625A78u;

// CPool<CObject>* CPools::ms_pObjectPool
inline constexpr std::uintptr_t kCPoolsObjectPool = 0x00625A90u;

// float CTimer::ms_fTimeStep
inline constexpr std::uintptr_t kCTimerTimeStep = 0x0082DA44u;

// uint32& CTimer::GetTimeInMilliseconds()
inline constexpr std::uintptr_t kCTimerGetTimeMs = 0x00293180u;

// CAutomobile::CAutomobile(int32 model, uint8 createdBy)
inline constexpr std::uintptr_t kCAutomobileCtor = 0x0028FCA0u;

// CCivilianPed::CCivilianPed(ePedType, uint32 model)
inline constexpr std::uintptr_t kCCivilianPedCtor = 0x001D06B0u;

// CPlayerPed::CPlayerPed()
inline constexpr std::uintptr_t kCPlayerPedCtor = 0x00219730u;

// CMenuManager FrontEndMenuManager
inline constexpr std::uintptr_t kFrontEndMenuManager = 0x00624600u;

// void CPed::Teleport(CVector) — заморозка игрока при вводе чата
inline constexpr std::uintptr_t kCPedTeleport = 0x001F0600u;

// void CPad::UpdatePads() — будущее отключение ввода
inline constexpr std::uintptr_t kCPadUpdatePads = 0x0010F920u;

// CKeyboardState CPad::NewKeyState
inline constexpr std::uintptr_t kCPadNewKeyState = 0x00625290u;

// void CKeyboardState::Clear()
inline constexpr std::uintptr_t kCKeyboardStateClear = 0x0010C2B0u;

// CPed* CPool<CPed>::New() — аллокация слота педа
inline constexpr std::uintptr_t kCPoolPedNew = 0x001DC700u;

// CVehicle* CPool<CVehicle>::New() — аллокация слота транспорта
inline constexpr std::uintptr_t kCPoolVehicleNew = 0x002D2570u;

// float CEntity::GetDistanceFromCentreOfMassToBaseOfModel() — высота покоя машины при спавне
inline constexpr std::uintptr_t kCEntityDistToBase = 0x00151DB0u;

// CStreamingInfo CStreaming::ms_aInfoForModel[] — m_loadState по +0x10, шаг 0x20 (HasModelLoaded)
inline constexpr std::uintptr_t kCStreamingInfoArray = 0x0062BCD0u;

// void CPed::SetObjective(eObjective, void*) — перед WarpPedIntoCar (ENTER_CAR_AS_DRIVER=18/PASSENGER=17)
inline constexpr std::uintptr_t kCPedSetObjective = 0x00205710u;

// void CPed::WarpPedIntoCar(CVehicle*) — мгновенная посадка педа в машину (как скриптовый WARP_CHAR_INTO_CAR)
inline constexpr std::uintptr_t kCPedWarpPedIntoCar = 0x001F20B0u;

// int CPed::GiveWeapon(eWeaponType, uint32, bool) — выдача оружия педу (модель в руку вешает сама игра)
inline constexpr std::uintptr_t kCPedGiveWeapon = 0x001DADA0u;

// void CPed::SetCurrentWeapon(eWeaponType) — взять оружие в руки
inline constexpr std::uintptr_t kCPedSetCurrentWeapon = 0x001E86D0u;

// void CPed::ProcessControl() — хук отключает ИИ сетевых кукол
inline constexpr std::uintptr_t kCPedProcessControl = 0x001E14E0u;

// void CPlaceable::SetHeading(float) — на будущее
inline constexpr std::uintptr_t kCPlaceableSetHeading = 0x00110710u;

// bool CSprite::CalcScreenCoors(in, out, w, h, check) — мир->экран для никнеймов
inline constexpr std::uintptr_t kCSpriteCalcScreenCoors = 0x002648A0u;

// void CPed::SetMoveState(eMoveState) — анимация ходьбы/бега куклы (следующий шаг)
inline constexpr std::uintptr_t kCPedSetMoveState = 0x001EEA90u;

// void CEntity::UpdateRwFrame() — пересчёт матрицы Rw-клампа (без него модель не едет за позицией)
inline constexpr std::uintptr_t kCEntityUpdateRwFrame = 0x00153580u;

// void CPed::SetMoveAnim() — применяет анимацию по m_nMoveState (сам проверяет смену)
inline constexpr std::uintptr_t kCPedSetMoveAnim = 0x001EE660u;

// CObject::CObject(int32 model, bool createRW) — конструктор серверного объекта/пикапа
inline constexpr std::uintptr_t kCObjectCtor = 0x001C7AF0u;

// CPool<CObject>::New() — слот пула объектов
inline constexpr std::uintptr_t kCPoolObjectNew = 0x001C88D0u;

// CPool<CPtrNode>* CPools::ms_pPtrNodePool
inline constexpr std::uintptr_t kCPoolsPtrNodePool = 0x00625A60u;

// CPool<CEntryInfoNode>* CPools::ms_pEntryInfoNodePool
inline constexpr std::uintptr_t kCPoolsEntryInfoNodePool = 0x00625A68u;

// CPool<CBuilding>* CPools::ms_pBuildingPool
inline constexpr std::uintptr_t kCPoolsBuildingPool = 0x00625A80u;

// CPool<CDummy>* CPools::ms_pDummyPool
inline constexpr std::uintptr_t kCPoolsDummyPool = 0x00625A98u;

// CModelInfo::GetModelInfo(name, &id) — IDE id модели по имени
inline constexpr std::uintptr_t kCModelInfoGetByName = 0x001C1F20u;

// uint64 CStreaming::ms_memoryAvailable — лимит памяти стриминга (байт)
inline constexpr std::uintptr_t kCStreamingMemoryAvailable = 0x0082D968u;

} // namespace vc::game::revc
