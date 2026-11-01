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

# --- App -----------------------------------------------------------------
add_executable(crystalvision
  src/main.cpp
  src/paths.cpp
)

target_compile_options(crystalvision PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
target_link_libraries(crystalvision PRIVATE PkgConfig::XCB OnnxRuntime::OnnxRuntime)
target_compile_definitions(crystalvision PRIVATE
  CV_DEV_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
)

if(CV_ENABLE_ASAN)
  target_compile_options(crystalvision PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(crystalvision PRIVATE -fsanitize=address,undefined)
endif()

# --- Install + .deb ------------------------------------------------------
include(Packaging)
