add_executable(vc-bot main.cpp)
set_target_properties(vc-bot PROPERTIES OUTPUT_NAME "vc-bot")
target_link_libraries(vc-bot PRIVATE vc::shared)
