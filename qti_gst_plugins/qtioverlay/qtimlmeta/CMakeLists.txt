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

# Common compiler flags.
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# GStreamer machine learning metadata.
set(GST_QTI_ML_META qtimlmeta)

add_library(${GST_QTI_ML_META} SHARED
  ml_meta.c
)

target_include_directories(${GST_QTI_ML_META} PUBLIC
  ${GST_INCLUDE_DIRS}
)
target_include_directories(${GST_QTI_ML_META} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_QTI_ML_META} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
)

install(TARGETS ${GST_QTI_ML_META} DESTINATION lib OPTIONAL)

FILE(GLOB INCLUDE_FILES "ml_meta.h")
INSTALL(FILES ${INCLUDE_FILES} DESTINATION include/ml-meta)
