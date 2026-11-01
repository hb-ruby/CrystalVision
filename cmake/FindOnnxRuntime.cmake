# Locates ONNX Runtime and defines the imported target OnnxRuntime::OnnxRuntime.
#
# Works with the Debian/Ubuntu package (libonnxruntime-dev) or an upstream
# release tarball. For a tarball, pass -DONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64-X.Y.Z

set(_ort_hints "${ONNXRUNTIME_ROOT}" "$ENV{ONNXRUNTIME_ROOT}")

find_path(ONNXRUNTIME_INCLUDE_DIR
  NAMES onnxruntime_cxx_api.h
  HINTS ${_ort_hints}
  PATH_SUFFIXES include include/onnxruntime include/onnxruntime/core/session onnxruntime
)

find_library(ONNXRUNTIME_LIBRARY
  NAMES onnxruntime
  HINTS ${_ort_hints}
  PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OnnxRuntime
  REQUIRED_VARS ONNXRUNTIME_LIBRARY ONNXRUNTIME_INCLUDE_DIR
)

if(OnnxRuntime_FOUND AND NOT TARGET OnnxRuntime::OnnxRuntime)
  add_library(OnnxRuntime::OnnxRuntime UNKNOWN IMPORTED)
  set_target_properties(OnnxRuntime::OnnxRuntime PROPERTIES
    IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
  )
endif()

mark_as_advanced(ONNXRUNTIME_INCLUDE_DIR ONNXRUNTIME_LIBRARY)
