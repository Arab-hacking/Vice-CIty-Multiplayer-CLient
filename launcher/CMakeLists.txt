add_executable(vc-launcher WIN32
    src/main.cpp
)
set_target_properties(vc-launcher PROPERTIES OUTPUT_NAME "vc-launcher")
target_link_libraries(vc-launcher PRIVATE vc::shared)

# TODO: выбрать UI (WinAPI/Qt/ImGui) — см. "Открытые решения"
# в docs/PROJECT-STRUCTURE.md. Тогда здесь появится find_package(...).
