cmake_minimum_required(VERSION 3.25)
project(ViceCityMultiplayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Клиентская DLL может существовать только как Win32/x86 —
# gta-vc.exe является 32-битным процессом.
option(VC_BUILD_CLIENT   "Собирать клиентскую DLL (только Windows x86)" ON)
option(VC_BUILD_LAUNCHER "Собирать лаунчер (только Windows)" ON)
option(VC_BUILD_SERVER   "Собирать игровой сервер" ON)
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

if(VC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
