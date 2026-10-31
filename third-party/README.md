# third-party/

Vendored-зависимости клиентской DLL (сборка без сети и vcpkg).
Каждая папка содержит файл лицензии библиотеки в неизменном виде.

| Папка | Что | Откуда | Лицензия |
|---|---|---|---|
| `imgui/` | Dear ImGui 1.91.5 (ядро + бэкенды Win32/DX9) — оверлей в игре | github.com/ocornut/imgui, тег `v1.91.5` | MIT (`imgui/LICENSE.txt`) |
| `minhook/` | MinHook 1.3.3 — хуки D3D9 EndScene/Reset | github.com/TsudaKageyu/minhook, тег `v1.3.3` | BSD-2 (`minhook/LICENSE.txt`) |

Остальной стек проекта внешних зависимостей пока не требует
(Lua для серверных скриптов добавится отдельным решением на Этапе 6).
