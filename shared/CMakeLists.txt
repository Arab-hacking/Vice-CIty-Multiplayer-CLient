add_library(vc-shared STATIC
    src/version.cpp
    src/net/bitstream.cpp
    src/net/raknet.cpp
    src/net/offline.cpp
    src/net/udp_socket.cpp
)
add_library(vc::shared ALIAS vc-shared)

target_include_directories(vc-shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(vc-shared PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(vc-shared PUBLIC ws2_32)
endif()
