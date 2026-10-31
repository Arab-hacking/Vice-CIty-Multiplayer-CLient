add_executable(vc-netprobe main.cpp)
set_target_properties(vc-netprobe PROPERTIES OUTPUT_NAME "vc-netprobe")
target_link_libraries(vc-netprobe PRIVATE vc::shared)
