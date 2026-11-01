# Install layout + .deb generation (CPack).
#
#   /usr/bin/crystalvision
#   /usr/lib/crystalvision/libonnxruntime.so.1      (bundled upstream ONNX Runtime)
#   /usr/share/crystalvision/models/640m.onnx        (NudeNet v3.4 weights)
#   /usr/share/doc/crystalvision/                    (licenses)

include(GNUInstallDirs)

# Private libs go under plain lib/, not Debian's lib/<triplet>.
set(CV_PRIVATE_LIBDIR "lib/crystalvision")

set_target_properties(crystalvision PROPERTIES
  INSTALL_RPATH "$ORIGIN/../${CV_PRIVATE_LIBDIR}"
)

install(TARGETS crystalvision RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/models/640m.onnx"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/crystalvision/models")

set(_docdir "${CMAKE_INSTALL_DATADIR}/doc/crystalvision")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION ${_docdir} RENAME copyright)

if(CV_BUNDLED_ORT_LIB)
  # Install the real file under its SONAME so the loader finds it via RPATH.
  install(FILES "${CV_BUNDLED_ORT_LIB}" DESTINATION ${CV_PRIVATE_LIBDIR}
    RENAME libonnxruntime.so.1
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
  install(FILES "${CV_BUNDLED_ORT_DIR}/LICENSE" DESTINATION ${_docdir}
    RENAME onnxruntime-LICENSE)
  install(FILES "${CV_BUNDLED_ORT_DIR}/ThirdPartyNotices.txt" DESTINATION ${_docdir}
    RENAME onnxruntime-ThirdPartyNotices.txt)
endif()

set(CV_MAINTAINER "hb-ruby <69754999+hb-ruby@users.noreply.github.com>" CACHE STRING "Debian package maintainer")

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME crystalvision)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "${CV_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "On-device nudity detection for the X11 desktop")
set(CPACK_PACKAGE_DESCRIPTION "CrystalVision watches the X11 desktop and detects nudity locally using the NudeNet v3 ONNX model. Nothing leaves the machine.")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/hb-ruby/CrystalVision")
# Work out Depends: (libxcb*, libc, libstdc++) automatically from the binary.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${CMAKE_INSTALL_PREFIX}/${CV_PRIVATE_LIBDIR}")

if(NOT CV_BUNDLED_ORT_LIB)
  message(STATUS "CV_SYSTEM_ORT=ON: the .deb would not bundle ONNX Runtime; build packages with the default instead.")
endif()

include(CPack)
