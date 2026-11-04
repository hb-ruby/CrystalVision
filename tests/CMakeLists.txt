# Each test is one executable linked against the core library. Exit code 77 means "can't run
# here" (no X display, no model) and is reported as skipped, not failed.
function(cv_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE crystalvision_core)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 ${ARGN})
endfunction()

cv_test(frame_test)
cv_test(detect_test)
cv_test(monitor_test)
cv_test(change_test)
cv_test(robustness_test)
# These two use the real X server or model, and change the screen briefly: one at a time.
cv_test(x11_test RUN_SERIAL ON)
cv_test(detector_test RUN_SERIAL ON TIMEOUT 600)
