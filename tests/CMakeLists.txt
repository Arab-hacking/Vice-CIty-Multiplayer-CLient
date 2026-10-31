add_executable(vc-unit-tests
    unit/test_bitstream_raknet.cpp
)
target_link_libraries(vc-unit-tests PRIVATE vc::shared)
add_test(NAME unit COMMAND vc-unit-tests)
