# Клиентская DLL под конкретный билд reVC.exe: СТРОГО x64
# (целевой reVC.exe — 64-битный, см. docs/REVC-BUILD.md).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "client: нужна 64-битная сборка (x64) под целевой reVC.exe. "
        "Используйте -A x64 / пресет windows-x64-*.")
endif()

add_library(vc-client SHARED
    src/dllmain.cpp
)
set_target_properties(vc-client PROPERTIES OUTPUT_NAME "vc-client")
target_link_libraries(vc-client PRIVATE vc::shared)

# Дальше по этапам: MinHook (хук CGame::Process), ImGui (D3D9-оверлей reVC),
# src/network (RakNet-клиент), src/pools, src/render.
