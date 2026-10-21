# Исследование игры

> Цель изменилась: клиент строится под **конкретный билд reVC (x64)**, не под
> оригинальный `gta-vc.exe 1.0`. Отпечаток билда и методика адресов из PDB —
> [REVC-BUILD.md](REVC-BUILD.md); сами RVA — `client/src/game/revc_addresses.h`
> (автогенерация `tools/gen_revc_addresses.py` из 19 748 символов PDB).

## Что уже найдено (RVA в revc_addresses.h)

- [x] Хук «каждый кадр» — кандидат `CGame::Process`
- [x] Рендер — Direct3D9 (импорт `d3d9.dll`), точка Present ещё предстоит
- [x] Локальный игрок: `FindPlayerPed/Vehicle/Coors`, `CWorld::Players`
- [x] Пулы: `CPools::ms_pPedPool/VehiclePool/ObjectPool`
- [x] Создание сущностей: ctor `CAutomobile/CCivilianPed/CPlayerPed`, `CWorld::Add/Remove`
- [x] Время/погода: `CClock::*`, `CWeather::*Type`; стриминг моделей: `CStreaming::RequestModel`

## Что предстоит

- [ ] Оффсеты полей структур из типов PDB (матрица/позиция CEntity, HP/оружие CPed)
- [ ] Функции отключения ИИ педа (для «кукол» удалённых игроков)
- [ ] Обработка урона и стрельбы (`CWeapon::Fire*`) — Этап 5

## Источники

- **PDB целевого билда reVC** — основной источник RVA (см. REVC-BUILD.md);
- исходники reVC соответствующей ревизии — смысл/signatures функций и структур;
- GTAMods Wiki, GTAForums (архивы тредов VC-MP);
- живая проверка из DLL (`vc-client.log`) и отладчиком: неверный адрес = краш без стектрейса.
