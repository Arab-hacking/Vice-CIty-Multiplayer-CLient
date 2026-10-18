cmake_minimum_required(VERSION 3.25)
# C нужен для vendored MinHook в клиенте.
project(ViceCityMultiplayer VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Цель клиента — конкретный билд reVC.exe (x64!). Адреса — из его PDB,
# см. docs/REVC-BUILD.md и client/src/game/revc_addresses.h.
option(VC_BUILD_CLIENT   "Собирать клиентскую DLL (только Windows x64)" ON)
option(VC_BUILD_LAUNCHER "Собирать лаунчер (только Windows)" ON)
option(VC_BUILD_SERVER   "Собирать игровой сервер" ON)
option(VC_BUILD_TOOLS    "Собирать утилиты (vc-netprobe)" ON)
option(VC_BUILD_TESTS    "Собирать тесты" OFF)

add_subdirectory(shared)

if(VC_BUILD_CLIENT)
    if(WIN32)
        add_subdirectory(client)
    else()
        message(WARNING "client: собирается только под Windows — пропущен")
    endif()
endif()

if(VC_BUILD_LAUNCHER)
    if(WIN32)
        add_subdirectory(launcher)
    else()
        message(WARNING "launcher: собирается только под Windows — пропущен")
    endif()
endif()

if(VC_BUILD_SERVER)
    add_subdirectory(server)
endif()

if(VC_BUILD_TOOLS)
    add_subdirectory(tools/netprobe)
endif()

if(VC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
