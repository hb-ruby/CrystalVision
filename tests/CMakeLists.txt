add_executable(frame_test frame_test.cpp)
target_link_libraries(frame_test PRIVATE crystalvision_core)
add_test(NAME frame_test COMMAND frame_test)

add_executable(detect_test detect_test.cpp)
target_link_libraries(detect_test PRIVATE crystalvision_core)
add_test(NAME detect_test COMMAND detect_test)

add_executable(monitor_test monitor_test.cpp)
target_link_libraries(monitor_test PRIVATE crystalvision_core)
add_test(NAME monitor_test COMMAND monitor_test)
