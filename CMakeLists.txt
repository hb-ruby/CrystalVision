cmake_minimum_required(VERSION 3.20)
project(CrystalVision VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(CV_ENABLE_ASAN "Build with AddressSanitizer + UBSan" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- X11 via XCB ---------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET
  xcb          # core protocol
  xcb-shm      # MIT-SHM: fast screen capture without copying through the socket
  xcb-composite # automatic redirection / overlay window
  xcb-xfixes   # input-shape regions (click-through overlay)
  xcb-shape
  xcb-image
)

# --- ONNX Runtime (C++ API) ----------------------------------------------
include(OnnxRuntime)

# --- Shared settings for all our targets ---------------------------------
add_library(crystalvision_options INTERFACE)
target_compile_options(crystalvision_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
if(CV_ENABLE_ASAN)
  target_compile_options(crystalvision_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(crystalvision_options INTERFACE -fsanitize=address,undefined)
endif()

# --- Core library (everything except main, so tests can link it) --------
add_library(crystalvision_core STATIC
  src/capture.cpp
  src/frame.cpp
  src/paths.cpp
  src/x11.cpp
)
target_include_directories(crystalvision_core PUBLIC src)
target_link_libraries(crystalvision_core PUBLIC crystalvision_options PkgConfig::XCB)
target_compile_definitions(crystalvision_core PRIVATE
  CV_DEV_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
)

# --- App -----------------------------------------------------------------
add_executable(crystalvision src/main.cpp)
target_link_libraries(crystalvision PRIVATE crystalvision_core OnnxRuntime::OnnxRuntime)

# --- Tests ---------------------------------------------------------------
option(CV_BUILD_TESTS "Build unit tests" ON)
if(CV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# --- Install + .deb ------------------------------------------------------
include(Packaging)
