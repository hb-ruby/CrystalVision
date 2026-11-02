add_executable(frame_test frame_test.cpp)
target_link_libraries(frame_test PRIVATE crystalvision_core)
add_test(NAME frame_test COMMAND frame_test)
