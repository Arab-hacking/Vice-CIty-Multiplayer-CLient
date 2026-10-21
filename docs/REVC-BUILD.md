# Целевой билд reVC

Клиент делается **не под оригинальный `gta-vc.exe`**, а под конкретный билд
**reVC** (передан архивом `файлы reVC.txt` = ZIP). Файлы из архива в репозиторий
НЕ коммитятся (чужие бинарники + лицензионная история reVC) — здесь фиксируем
только отпечатки и выведенные из PDB RVA.

## Отпечаток билда (проверять перед запуском клиента!)

| | |
|---|---|
| `reVC.exe` | 4 559 872 байт, **x64** (Machine 0x8664), ImageBase `0x140000000` |
| SHA256 exe | `5af4eb9122823c830b5d44ddfd7fabd2fa1f4db2fbd894e1240cef8e8450b3d0` |
| `reVC.pdb` | 22 982 656 байт, SHA256 `044c489de7fcaf751ac293937c1a306e748db08b1fd14cac95d4bdeb951174b3` |
| RSDS GUID в exe | `0d644e59-278c-4dfa-b9ec-596cc7494efd`, age 1, путь `reVC.pdb` |
| PE timestamp (UTC) | 2026-10-17 16:16:38 |

История билдов: первый присланный (2026-10-17 15:04, SHA256 `b2da8828...`) заменён
этим — у владельца на машине оказался более свежий exe, и защита по прологу хука
корректно отказалась ставиться. С тех пор DLL сама сверяет SHA256 exe при старте.
| Подсистема/флаги | Windows GUI; ASLR + NX (DllCharacteristics 0x8160) |
| Ключевые импорты | **d3d9.dll** (рендер — Direct3D9, не D3D8!), DINPUT8, XINPUT9_1_0, OpenAL32, libmpg123-0, WINMM, DDRAW |
| Рядом в архиве | `gamecontrollerdb.txt`, `OpenAL32.dll`, `libmpg123-0.dll` |

## Следствия для клиента

1. **Клиентская DLL — x64**, лаунчер-инжектор тоже x64 (пресеты `windows-x64-*`).
2. **Адреса = база модуля + RVA** из PDB (ASLR) — см. `vc::game::Resolve` в `client/src/game/revc.h`.
3. **Оверлей рисуем хуком Direct3D9**, а не D3D8.
4. Пересоберёшь reVC — адреса уедут: перегенерируй заголовок (ниже) под новый PDB.

## Как получены адреса

Из `reVC.pdb` извлечено 19 748 символов (имя → RVA) чистым Python-парсером PDB
(volatility3 `PdbReader`; разбор типов TPI/IPI отключён — нужны только символы).
Нужные клиенту 26 символов отобраны в `tools/gen_revc_addresses.py` и сгенерированы
в `client/src/game/revc_addresses.h`, в т.ч.:

- `FindPlayerPed/Vehicle/Coors` — локальный игрок;
- `CWorld::Players/Add/Remove` — сущности мира, основа спавна сетевых педов;
- `CPools::ms_pPedPool/VehiclePool/ObjectPool` — пулы reVC;
- `CAutomobile/CCivilianPed/CPlayerPed` ctor — создание машин/педов (Этап 3–4);
- `CGame::Process` — кандидат на хук «каждый кадр» (Этап 1.5);
- `CClock/CWeather`, `CStreaming::RequestModel`, `CTimer`, `TheCamera`, `FrontEndMenuManager`.

Перегенерация под новый билд:

```powershell
pip install volatility3
python tools/gen_revc_addresses.py --pdb путь\reVC.pdb `
  --exe-sha256 <sha256 нового reVC.exe> --pdb-guid <RSDS GUID>
```

## Что ещё предстоит по реверсу этого билда

- Оффсеты полей структур (`CPlayerPed`, `CVehicle`, `CEntity::m_matrix`, HP, оружие) —
  достаются из типов PDB (TPI) или сверкой с исходниками reVC соответствующей ревизии.
- Точка D3D9 Present/EndScene для оверлея (reVC ходит через RenderWare D3D9 backend).
- Проверка: `FindPlayerPed()` из нашей DLL после загрузки мира возвращает валидный пед.
