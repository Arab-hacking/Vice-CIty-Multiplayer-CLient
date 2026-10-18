add_library(vc-shared STATIC
    src/version.cpp
)
add_library(vc::shared ALIAS vc-shared)

target_include_directories(vc-shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(vc-shared PUBLIC cxx_std_20)
