#!/usr/bin/env python3
"""
Генератор client/src/game/revc_addresses.h из PDB целевого билда reVC.

Зачем: reVC.exe собран с ASLR, поэтому клиенту нужны RVA символов именно
ТОГО билда, с которым он будет работать. Адреса берём из reVC.pdb
(сами exe/pdb в репозиторий НЕ коммитятся).

Использование:
  # вариант 1: напрямую из PDB (нужен volatility3: pip install volatility3)
  python tools/gen_revc_addresses.py --pdb path/to/reVC.pdb \
      --exe-sha256 <sha256 reVC.exe> --pdb-guid <guid из RSDS>

  # вариант 2: из ранее извлечённого JSON {имя: {"address": RVA}}
  python tools/gen_revc_addresses.py --symbols-json symbols.json ...

Вывод по умолчанию: client/src/game/revc_addresses.h (от корня репозитория).
"""
import argparse
import json
import pathlib
import sys

# Символы reVC, нужные клиенту: (имя константы, манглированное имя в PDB, комментарий)
CURATED = [
    ("kFindPlayerPed", "?FindPlayerPed@@YAPEAVCPlayerPed@@XZ", "CPlayerPed* FindPlayerPed()"),
    ("kFindPlayerVehicle", "?FindPlayerVehicle@@YAPEAVCVehicle@@XZ", "CVehicle* FindPlayerVehicle()"),
    ("kFindPlayerCoors", "?FindPlayerCoors@@YA?AVCVector@@XZ", "CVector FindPlayerCoors()"),
    ("kCWorldPlayers", "?Players@CWorld@@2PAVCPlayerInfo@@A", "CPlayerInfo* CWorld::Players[MAX_PLAYERS]"),
    ("kCWorldAdd", "?Add@CWorld@@SAXPEAVCEntity@@@Z", "void CWorld::Add(CEntity*)"),
    ("kCWorldRemove", "?Remove@CWorld@@SAXPEAVCEntity@@@Z", "void CWorld::Remove(CEntity*)"),
    ("kCGameInitialise", "?Initialise@CGame@@SA_NPEBD@Z", "bool CGame::Initialise(const char*)"),
    ("kCGameProcess", "?Process@CGame@@SAXXZ", "void CGame::Process() — кандидат на хук кадра"),
    ("kCClockHours", "?ms_nGameClockHours@CClock@@2EA", "uint8 CClock::ms_nGameClockHours"),
    ("kCClockMinutes", "?ms_nGameClockMinutes@CClock@@2EA", "uint8 CClock::ms_nGameClockMinutes"),
    ("kCClockSeconds", "?ms_nGameClockSeconds@CClock@@2GA", "uint16 CClock::ms_nGameClockSeconds"),
    ("kCWeatherForcedType", "?ForcedWeatherType@CWeather@@2FA", "int16 CWeather::ForcedWeatherType"),
    ("kCWeatherNewType", "?NewWeatherType@CWeather@@2FA", "int16 CWeather::NewWeatherType"),
    ("kCWeatherOldType", "?OldWeatherType@CWeather@@2FA", "int16 CWeather::OldWeatherType"),
    ("kTheCamera", "?TheCamera@@3VCCamera@@A", "CCamera TheCamera"),
    ("kCStreamingRequestModel", "?RequestModel@CStreaming@@SAXHH@Z", "void CStreaming::RequestModel(int32, int32)"),
    ("kCStreamingLoadAllRequested", "?LoadAllRequestedModels@CStreaming@@SAX_N@Z", "void CStreaming::LoadAllRequestedModels(bool)"),
    ("kCPoolsPedPool", "?ms_pPedPool@CPools@@0PEAV?$CPool@VCPed@@VCPlayerPed@@@@EA", "CPool<CPed>* CPools::ms_pPedPool"),
    ("kCPoolsVehiclePool", "?ms_pVehiclePool@CPools@@0PEAV?$CPool@VCVehicle@@VCAutomobile@@@@EA", "CPool<CVehicle>* CPools::ms_pVehiclePool"),
    ("kCPoolsObjectPool", "?ms_pObjectPool@CPools@@0PEAV?$CPool@VCObject@@VCCutsceneObject@@@@EA", "CPool<CObject>* CPools::ms_pObjectPool"),
    ("kCTimerTimeStep", "?ms_fTimeStep@CTimer@@0MA", "float CTimer::ms_fTimeStep"),
    ("kCTimerGetTimeMs", "?GetTimeInMilliseconds@CTimer@@SAAEBIXZ", "uint32& CTimer::GetTimeInMilliseconds()"),
    ("kCAutomobileCtor", "??0CAutomobile@@QEAA@HE@Z", "CAutomobile::CAutomobile(int32 model, uint8 createdBy)"),
    ("kCCivilianPedCtor", "??0CCivilianPed@@QEAA@W4ePedType@@I@Z", "CCivilianPed::CCivilianPed(ePedType, uint32 model)"),
    ("kCPlayerPedCtor", "??0CPlayerPed@@QEAA@XZ", "CPlayerPed::CPlayerPed()"),
    ("kFrontEndMenuManager", "?FrontEndMenuManager@@3VCMenuManager@@A", "CMenuManager FrontEndMenuManager"),
    ("kCPedTeleport", "?Teleport@CPed@@UEAAXVCVector@@@Z", "void CPed::Teleport(CVector) — заморозка игрока при вводе чата"),
    ("kCPadUpdatePads", "?UpdatePads@CPad@@SAXXZ", "void CPad::UpdatePads() — будущее отключение ввода"),
    ("kCPadNewKeyState", "?NewKeyState@CPad@@2VCKeyboardState@@A", "CKeyboardState CPad::NewKeyState"),
    ("kCKeyboardStateClear", "?Clear@CKeyboardState@@QEAAXXZ", "void CKeyboardState::Clear()"),
]


def load_symbols_from_pdb(pdb_path: pathlib.Path) -> dict:
    """Извлечение символов через volatility3 (чистый Python-парсер PDB)."""
    from volatility3.framework import contexts
    from volatility3.framework.symbols.windows import pdbconv

    # Новым PDB не нужен разбор типов — нужны только символы.
    pdbconv.PdbReader.read_ipi_stream = lambda self: None
    pdbconv.PdbReader.read_tpi_stream = lambda self: None
    reader = pdbconv.PdbReader(contexts.Context(), pdb_path.resolve().as_uri())
    data = reader.get_json()
    if isinstance(data, str):
        data = json.loads(data)
    return data.get("symbols", {})


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--pdb", type=pathlib.Path)
    src.add_argument("--symbols-json", type=pathlib.Path)
    ap.add_argument("--exe-sha256", default="unknown")
    ap.add_argument("--pdb-guid", default="unknown")
    ap.add_argument("--out", type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parent.parent
                            / "client/src/game/revc_addresses.h")
    args = ap.parse_args()

    if args.pdb:
        symbols = load_symbols_from_pdb(args.pdb)
    else:
        symbols = json.loads(args.symbols_json.read_text(encoding="utf-8"))

    lines = [
        "#pragma once",
        "",
        "// АВТОГЕНЕРАЦИЯ: tools/gen_revc_addresses.py — НЕ редактировать руками.",
        "// RVA символов целевого билда reVC.exe (x64, ASLR): реальный адрес =",
        "// база модуля reVC.exe в процессе + RVA (см. vc::game::Resolve).",
        f"// reVC.exe SHA256: {args.exe_sha256}",
        f"// reVC.pdb RSDS GUID: {args.pdb_guid}",
        "",
        "#include <cstdint>",
        "",
        "namespace vc::game::revc {",
        "",
        f'inline constexpr const char* kTargetExeSha256 = "{args.exe_sha256}";',
        f'inline constexpr const char* kTargetPdbGuid = "{args.pdb_guid}";',
        "",
    ]
    missing = []
    for const_name, mangled, comment in CURATED:
        entry = symbols.get(mangled)
        if entry is None:
            missing.append(mangled)
            lines.append(f"// ОТСУТСТВУЕТ В PDB: {mangled}  ({comment})")
            lines.append(f"inline constexpr std::uintptr_t {const_name} = 0;")
        else:
            rva = entry["address"] if isinstance(entry, dict) else int(entry)
            lines.append(f"// {comment}")
            lines.append(f"inline constexpr std::uintptr_t {const_name} = 0x{rva:08X}u;")
        lines.append("")
    lines.append("} // namespace vc::game::revc")
    lines.append("")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"symbols total: {len(symbols)}, written: {len(CURATED) - len(missing)}, "
          f"missing: {len(missing)} -> {args.out}")
    for m in missing:
        print("  MISSING:", m)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
