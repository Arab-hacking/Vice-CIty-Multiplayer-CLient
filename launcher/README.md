# launcher/ — лаунчер игрока (`vc-launcher.exe`)

То, что запускает игрок вместо самой игры (аналог SA-MP server browser).

Обязанности:
- список серверов: вкладки Internet (master-server) / Favorites / LAN, пинг и онлайн (query-пакеты);
- настройки: ник, путь к GTA VC, проверка версии `gta-vc.exe` (строго 1.0 US);
- кнопка «Играть»: запуск игры в `CREATE_SUSPENDED` → инжект `vc-client.dll` (`injector.cpp`) →
  передача параметров подключения (IP, порт, ник) → возобновление процесса;
- позже: автообновление клиента.

Открытый вопрос: UI на WinAPI / Qt6 / ImGui — см. [PROJECT-STRUCTURE](../docs/PROJECT-STRUCTURE.md).
Статус: каркас с заглушечным окном.
