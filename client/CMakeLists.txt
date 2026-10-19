# Клиентская DLL под конкретный билд reVC.exe: СТРОГО x64
# (целевой reVC.exe — 64-битный, см. docs/REVC-BUILD.md).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "client: нужна 64-битная сборка (x64) под целевой reVC.exe. "
        "Используйте -A x64 / пресет windows-x64-*.")
endif()

set(VC_THIRD_PARTY ${CMAKE_SOURCE_DIR}/third-party)

# Vendored MinHook (BSD-2) — только x64-части + общие.
add_library(vc-minhook STATIC
    ${VC_THIRD_PARTY}/minhook/src/hook.c
    ${VC_THIRD_PARTY}/minhook/src/buffer.c
    ${VC_THIRD_PARTY}/minhook/src/trampoline.c
    ${VC_THIRD_PARTY}/minhook/src/hde/hde64.c
)
target_include_directories(vc-minhook PUBLIC ${VC_THIRD_PARTY}/minhook/include)

# Vendored Dear ImGui (MIT) + Win32/DX9 бэкенды для оверлея.
add_library(vc-imgui STATIC
    ${VC_THIRD_PARTY}/imgui/imgui.cpp
    ${VC_THIRD_PARTY}/imgui/imgui_draw.cpp
    ${VC_THIRD_PARTY}/imgui/imgui_tables.cpp
    ${VC_THIRD_PARTY}/imgui/imgui_widgets.cpp
    ${VC_THIRD_PARTY}/imgui/backends/imgui_impl_win32.cpp
    ${VC_THIRD_PARTY}/imgui/backends/imgui_impl_dx9.cpp
)
target_include_directories(vc-imgui PUBLIC ${VC_THIRD_PARTY}/imgui)
target_compile_features(vc-imgui PUBLIC cxx_std_20)

add_library(vc-client SHARED
    src/dllmain.cpp
    src/game/hooks/hook_cgame_process.cpp
    src/network/net_client.cpp
    src/pools/player_pool.cpp
    src/pools/vehicle_pool.cpp
    src/render/overlay.cpp
)
set_target_properties(vc-client PROPERTIES OUTPUT_NAME "vc-client")
target_link_libraries(vc-client PRIVATE vc::shared vc-minhook vc-imgui d3d9 gdi32 dwmapi)

# Дальше по этапам: src/network (RakNet-клиент), src/pools, чат в render.
