# create by Ricardo Lu in 07/15/2022

cmake_minimum_required(VERSION 3.10)

project(ds-yolov5s)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCV REQUIRED)
find_package(spdlog REQUIRED)

include(FindPkgConfig)
pkg_check_modules(GST    REQUIRED gstreamer-1.0)
pkg_check_modules(GSTAPP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GLIB   REQUIRED glib-2.0)
pkg_check_modules(GFLAGS REQUIRED gflags)
pkg_check_modules(JSON   REQUIRED json-glib-1.0)

set(DeepStream_ROOT "/opt/nvidia/deepstream/deepstream-6.1")
set(DeepStream_INCLUDE_DIRS "${DeepStream_ROOT}/sources/includes")
set(DeepStream_LIBRARY_DIRS "${DeepStream_ROOT}/lib")

message(STATUS "GST:   ${GST_INCLUDE_DIRS},${GST_LIBRARY_DIRS},${GST_LIBRARIES}")
message(STATUS "GSTAPP:${GSTAPP_INCLUDE_DIRS},${GSTAPP_LIBRARY_DIRS},${GSTAPP_LIBRARIES}")
message(STATUS "GLIB:  ${GLIB_INCLUDE_DIRS},${GLIB_LIBRARY_DIRS},${GLIB_LIBRARIES}")
message(STATUS "JSON:  ${JSON_INCLUDE_DIRS},${JSON_LIBRARY_DIRS},${JSON_LIBRARIES}")
message(STATUS "GFLAGS:${GFLAGS_INCLUDE_DIRS},${GFLAGS_LIBRARY_DIRS},${GFLAGS_LIBRARIES}")
message(STATUS "OpenCV:${OpenCV_INCLUDE_DIRS},${OpenCV_LIBRARY_DIRS},${OpenCV_LIBRARIES}")
message(STATUS "DeepStream: ${DeepStream_INCLUDE_DIRS}, ${DeepStream_LIBRARY_DIRS}")

include_directories(
    ${PROJECT_SOURCE_DIR}/inc
    ${GST_INCLUDE_DIRS}
    ${GSTAPP_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    ${GFLAGS_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${spdlog_INCLUDE_DIRS}
    ${DeepStream_INCLUDE_DIRS}
)

link_directories(
    ${GST_LIBRARY_DIRS}
    ${GSTAPP_LIBRARY_DIRS}
    ${GLIB_LIBRARY_DIRS}
    ${GFLAGS_LIBRARY_DIRS}
    ${JSON_LIBRARY_DIRS}
    ${OpenCV_LIBRARY_DIRS}
    ${spdlog_LIBRARY_DIRS}
    ${DeepStream_LIBRARY_DIRS}
)

# Config Logger
if(NOT DEFINED LOG_LEVEL)
    message(STATUS "Not define log print level, default is 'info'")
    set(LOG_LEVEL "info")
endif()
add_definitions(-DLOG_LEVEL="${LOG_LEVEL}")
message(STATUS "log level: ${LOG_LEVEL}")

option(DUMP_LOG "Dump log into a file." OFF)
option(MULTI_LOG "Dump log and stdout." OFF)

if(DUMP_LOG OR MULTI_LOG)
    if(NOT DEFINED LOG_PATH)
        message(STATUS "Not define log path, use default")
        set(LOG_PATH "./log")
        message(STATUS "log path: ${LOG_PATH}")
    endif()
    if(NOT DEFINED LOG_FILE_PREFIX)
        message(STATUS "Not define log name prefix, use default")
        set(LOG_FILE_PREFIX ${PROJECT_NAME})
        message(STATUS "log file prefix: ${LOG_FILE_PREFIX}")
    endif()

    add_definitions(
        -DDUMP_LOG
        -DLOG_PATH="${LOG_PATH}"
        -DLOG_FILE_PREFIX="${LOG_FILE_PREFIX}"
    )
    if(MULTI_LOG)
        message(STATUS "Multi log set.")
        add_definitions(-DMULTI_LOG)
    endif()
endif()
# End Config Logger

add_executable(${PROJECT_NAME}
    src/VideoPipeline.cpp
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME}
    ${GST_LIBRARIES}
    ${GSTAPP_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${GFLAGS_LIBRARIES}
    ${JSON_LIBRARIES}
    ${OpenCV_LIBRARIES}
    nvbufsurface
    nvdsgst_meta
    nvds_meta
    nvds_utils
)