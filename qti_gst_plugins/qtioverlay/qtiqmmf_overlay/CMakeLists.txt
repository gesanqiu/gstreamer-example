cmake_minimum_required(VERSION 3.1)

project(qmmf_overlay)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUSE_SKIA=0 -DUSE_CAIRO=1")

if (NOT OVERLAY_ENABLED)
set(exclude EXCLUDE_FROM_ALL)
endif()

#add_definitions(-DOVERLAY_OPEN_CL_BLIT)

add_library(qmmf_overlay SHARED ${exclude}
${CMAKE_CURRENT_SOURCE_DIR}/qmmf_overlay.cc
)

target_include_directories(qmmf_overlay
 PRIVATE ${TOP_DIRECTORY})

target_include_directories(qmmf_overlay
 PRIVATE $<BUILD_INTERFACE:${KERNEL_INCDIR}/usr/include>)

target_include_directories(qmmf_overlay
 PRIVATE ${TOP_DIRECTORY}/common/memory)

# TODO remove this hack when camx issue with propagating c and cpp glags is solved
target_include_directories(qmmf_overlay
 PRIVATE ${PKG_CONFIG_SYSROOT_DIR}/usr/include/ion_headers)

install(TARGETS qmmf_overlay DESTINATION lib OPTIONAL)
target_link_libraries(qmmf_overlay log binder pthread utils cutils dl C2D2 cairo OpenCL qmmf_utils)

# TODO remove this hack when camx issue with propagating c and cpp glags is solved
target_link_libraries(qmmf_overlay ion)

file(GLOB_RECURSE RGB_FILES ${CMAKE_CURRENT_LIST_DIR}/raw_image/*.rgba)
install(FILES ${RGB_FILES} DESTINATION ${QMMF_DATA})

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/overlay_blit_kernel.cl
    DESTINATION /usr/lib/qmmf
)
