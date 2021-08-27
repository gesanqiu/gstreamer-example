cmake_minimum_required(VERSION 3.10)
project(build_pipeline)

set(CMAKE_CXX_STANDARD 11)

include(FindPkgConfig)
pkg_check_modules(GST    REQUIRED gstreamer-1.0)
pkg_check_modules(GSTAPP REQUIRED gstreamer-app-1.0)

include_directories(
    ${GST_INCLUDE_DIRS}
    ${GSTAPP_INCLUDE_DIRS}
)

link_directories(
    ${GST_LIBRARY_DIRS}
    ${GSTAPP_LIBRARY_DIRS}
)

OPTION(COMPILE_PARSE_LAUNCH "build gst_parse_launch" OFF)
OPTION(COMPILE_FACTORY_MAKE "build gst_element_factory_make" OFF)

if(COMPILE_PARSE_LAUNCH)
add_definitions(-DPARSE_LAUNCH)

add_executable(GstParse
    gst_parse_launch.cpp
)

target_link_libraries(GstParse
    ${GST_LIBRARIES}
    ${GSTAPP_LIBRARIES}
)
endif(COMPILE_PARSE_LAUNCH)

if(COMPILE_FACTORY_MAKE)
add_definitions(-DFACTORY_MAKE)

add_executable(GstElementFactory
    gst_element_factory_make.cpp
)

target_link_libraries(GstElementFactory
    ${GST_LIBRARIES}
    ${GSTAPP_LIBRARIES}
)
endif(COMPILE_FACTORY_MAKE)