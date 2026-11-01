# Provides the imported target OnnxRuntime::OnnxRuntime.
#
# Default: download Microsoft's official prebuilt release (pinned + hash-checked).
# It needs only glibc 2.28, so the same library runs on Linux Mint, Ubuntu and Debian,
# and it gets bundled into the .deb under /usr/lib/crystalvision/.
#
# -DCV_SYSTEM_ORT=ON uses a distro package (e.g. Debian's libonnxruntime-dev) instead.
# That build is for development only and can't be packaged.

option(CV_SYSTEM_ORT "Use the system ONNX Runtime instead of the bundled upstream release" OFF)

set(CV_ORT_VERSION 1.31.0)
set(CV_ORT_SHA256 cc5c72baf5ae5c8238a6841f0897227be2d02826b9cf98eaf02fdefaeeb03a57)

if(CV_SYSTEM_ORT)
  find_package(OnnxRuntime REQUIRED) # cmake/FindOnnxRuntime.cmake
  set(CV_BUNDLED_ORT_LIB "")
  return()
endif()

include(FetchContent)
FetchContent_Declare(onnxruntime_prebuilt
  URL "https://github.com/microsoft/onnxruntime/releases/download/v${CV_ORT_VERSION}/onnxruntime-linux-x64-${CV_ORT_VERSION}.tgz"
  URL_HASH SHA256=${CV_ORT_SHA256}
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(onnxruntime_prebuilt)

set(_ort_dir "${onnxruntime_prebuilt_SOURCE_DIR}")
set(CV_BUNDLED_ORT_LIB "${_ort_dir}/lib/libonnxruntime.so.${CV_ORT_VERSION}")
set(CV_BUNDLED_ORT_DIR "${_ort_dir}")

add_library(OnnxRuntime::OnnxRuntime SHARED IMPORTED)
set_target_properties(OnnxRuntime::OnnxRuntime PROPERTIES
  IMPORTED_LOCATION "${CV_BUNDLED_ORT_LIB}"
  IMPORTED_SONAME "libonnxruntime.so.1"
  INTERFACE_INCLUDE_DIRECTORIES "${_ort_dir}/include"
)
