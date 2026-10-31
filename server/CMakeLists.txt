add_executable(vc-server
    src/main.cpp
)
set_target_properties(vc-server PROPERTIES OUTPUT_NAME "vc-server")
target_link_libraries(vc-server PRIVATE vc::shared)

# Сюда добавятся: enet (сеть), lua (игровые режимы) — Этапы 2 и 6.
