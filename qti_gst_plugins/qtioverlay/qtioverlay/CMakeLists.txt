cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_OVERLAY
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC
  REQUIRED gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -DUSE_SKIA=0 -DUSE_CAIRO=1")

# GStreamer plugin.
set(GST_QTI_OVERLAY qtioverlay)

add_library(${GST_QTI_OVERLAY} SHARED
  gstoverlay.cc
)

target_include_directories(${GST_QTI_OVERLAY} PUBLIC
  ${GST_INCLUDE_DIRS}
)
target_include_directories(${GST_QTI_OVERLAY} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_QTI_OVERLAY} PRIVATE
  qmmf_overlay
  qtimlmeta
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_OVERLAY}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              GROUP_EXECUTE GROUP_READ
)
