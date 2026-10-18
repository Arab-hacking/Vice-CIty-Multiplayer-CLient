# Клиентская DLL: СТРОГО x86, внедряется в 32-битный gta-vc.exe.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "client: нужна 32-битная сборка (Win32). "
        "Используйте пресет windows-x86-debug / windows-x86-release.")
endif()

add_library(vc-client SHARED
    src/dllmain.cpp
)
set_target_properties(vc-client PROPERTIES OUTPUT_NAME "vc-client")
target_link_libraries(vc-client PRIVATE vc::shared)

# Сюда добавятся на следующих этапах:
#   find_package(imgui CONFIG REQUIRED)  / minhook / enet
#   src/game/hooks/*.cpp, src/network/*.cpp, src/pools/*.cpp, src/render/*.cpp
