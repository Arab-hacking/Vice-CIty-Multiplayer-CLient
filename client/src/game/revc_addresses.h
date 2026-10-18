#pragma once

// АВТОГЕНЕРАЦИЯ: tools/gen_revc_addresses.py — НЕ редактировать руками.
// RVA символов целевого билда reVC.exe (x64, ASLR): реальный адрес =
// база модуля reVC.exe в процессе + RVA (см. vc::game::Resolve).
// reVC.exe SHA256: b2da882847fcd98974013f670651075064a09def9c616ffa7e0e084b3790ddf9
// reVC.pdb RSDS GUID: da7c3851-3404-4cb9-859f-ed3a1fee9625

#include <cstdint>

namespace vc::game::revc {

inline constexpr const char* kTargetExeSha256 = "b2da882847fcd98974013f670651075064a09def9c616ffa7e0e084b3790ddf9";
inline constexpr const char* kTargetPdbGuid = "da7c3851-3404-4cb9-859f-ed3a1fee9625";

// CPlayerPed* FindPlayerPed()
inline constexpr std::uintptr_t kFindPlayerPed = 0x0010EF80u;

// CVehicle* FindPlayerVehicle()
inline constexpr std::uintptr_t kFindPlayerVehicle = 0x0010F030u;

// CVector FindPlayerCoors()
inline constexpr std::uintptr_t kFindPlayerCoors = 0x0010EE50u;

// CPlayerInfo* CWorld::Players[MAX_PLAYERS]
inline constexpr std::uintptr_t kCWorldPlayers = 0x00825A50u;

// void CWorld::Add(CEntity*)
inline constexpr std::uintptr_t kCWorldAdd = 0x0012AA70u;

// void CWorld::Remove(CEntity*)
inline constexpr std::uintptr_t kCWorldRemove = 0x00133710u;

// bool CGame::Initialise(const char*)
inline constexpr std::uintptr_t kCGameInitialise = 0x00105200u;

// void CGame::Process() — кандидат на хук кадра
inline constexpr std::uintptr_t kCGameProcess = 0x001060C0u;

// uint8 CClock::ms_nGameClockHours
inline constexpr std::uintptr_t kCClockHours = 0x0061091Cu;

// uint8 CClock::ms_nGameClockMinutes
inline constexpr std::uintptr_t kCClockMinutes = 0x0061091Du;

// uint16 CClock::ms_nGameClockSeconds
inline constexpr std::uintptr_t kCClockSeconds = 0x00610920u;

// int16 CWeather::ForcedWeatherType
inline constexpr std::uintptr_t kCWeatherForcedType = 0x00E6A91Cu;

// int16 CWeather::NewWeatherType
inline constexpr std::uintptr_t kCWeatherNewType = 0x00E6A918u;

// int16 CWeather::OldWeatherType
inline constexpr std::uintptr_t kCWeatherOldType = 0x00E6A914u;

// CCamera TheCamera
inline constexpr std::uintptr_t kTheCamera = 0x0060B690u;

// void CStreaming::RequestModel(int32, int32)
inline constexpr std::uintptr_t kCStreamingRequestModel = 0x00126E70u;

// void CStreaming::LoadAllRequestedModels(bool)
inline constexpr std::uintptr_t kCStreamingLoadAllRequested = 0x00123640u;

// CPool<CPed>* CPools::ms_pPedPool
inline constexpr std::uintptr_t kCPoolsPedPool = 0x0061CD70u;

// CPool<CVehicle>* CPools::ms_pVehiclePool
inline constexpr std::uintptr_t kCPoolsVehiclePool = 0x0061CD78u;

// CPool<CObject>* CPools::ms_pObjectPool
inline constexpr std::uintptr_t kCPoolsObjectPool = 0x0061CD90u;

// float CTimer::ms_fTimeStep
inline constexpr std::uintptr_t kCTimerTimeStep = 0x00824D44u;

// uint32& CTimer::GetTimeInMilliseconds()
inline constexpr std::uintptr_t kCTimerGetTimeMs = 0x00289A90u;

// CAutomobile::CAutomobile(int32 model, uint8 createdBy)
inline constexpr std::uintptr_t kCAutomobileCtor = 0x00286530u;

// CCivilianPed::CCivilianPed(ePedType, uint32 model)
inline constexpr std::uintptr_t kCCivilianPedCtor = 0x001CAA60u;

// CPlayerPed::CPlayerPed()
inline constexpr std::uintptr_t kCPlayerPedCtor = 0x002139C0u;

// CMenuManager FrontEndMenuManager
inline constexpr std::uintptr_t kFrontEndMenuManager = 0x0061B900u;

} // namespace vc::game::revc
