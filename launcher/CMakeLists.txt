add_executable(vc-launcher WIN32
    src/main.cpp
)
set_target_properties(vc-launcher PROPERTIES OUTPUT_NAME "vc-launcher")
# wWinMain требует UNICODE, иначе MSVC ищет WinMain и линковка падает.
target_compile_definitions(vc-launcher PRIVATE UNICODE _UNICODE)
target_link_libraries(vc-launcher PRIVATE vc::shared)

# TODO: выбрать UI (WinAPI/Qt/ImGui) — см. "Открытые решения"
# в docs/PROJECT-STRUCTURE.md. Тогда здесь появится find_package(...).
