cmake_minimum_required(VERSION 3.10)
project(flir-vvcam-driver C)

# Required defines for ARM64 build
# add_definitions(
#     -DCMAKE_BUILD_TYPE=debug
#     -DISP_VERSION=ISP8000NANO_V1802
#     -DPLATFORM=ARM64
#     -DLINUX
#     -DLIVE_SENSOR
#     -DTUNINGEXT=1
#     -DQTLESS=1
#     -DFULL_SRC_COMPILE=1
#     -DWITH_DWE=1
#     -DWITH_DRM=1
#     -DSUBDEV_V4L2=1
# 	-DIMX_G2D=ON
# )

# add_definitions(-DISI_LITE)
add_definitions(-DLINUX)
# add_definitions(-DMARVIN_HW_ALTERA)
# add_definitions(-DMIPI_USE_CAMERIC)
add_definitions(-DHAL_ALTERA)
# add_definitions(-DCAM_ENGINE_DRAW_DOM_ONLY)
# add_definitions(-DARM64)
# add_definitions(-DLIVE_SENSOR)
# set(LIB_PTHREAD_NAME "pthread")
# set(LIB_RT_NAME "rt")
add_definitions(-DSUBDEV_V4L2=1)
# add_definitions(-Werror)
# add_definitions(-DISP_VERSION=ISP8000NANO_V1802)
# add_definitions(-DISP8000NANO_V1802)
# add_definitions(-DISP8000NANO_BASE)

add_definitions(-DWITH_DWE=1)
# add_definitions(-DWITH_DRM=1)
# add_definitions(-DIMX_G2D=ON)
# add_definitions(-DISP_COMPAND)
# add_definitions(-DISP_DVP_PINMAPPING)
# add_definitions(-DISP_FILTER)
# # add_definitions(-DISP_HDR_STITCH)
# add_definitions(-DISP_MIV1)
# add_definitions(-DISP_MI_BUF_DUMP)
# add_definitions(-DISP_MP_34BIT)
# add_definitions(-DISP_MULTI_NANO)
# add_definitions(-DISP_SINGLE_DOM_OVER_MP)
# add_definitions(-DISP_TPG)
# add_definitions(-DISP_WDR_V3)
# add_definitions(-DSUPPORT_PASS_JSON)

# Source directory (can be overridden)
if(NOT DEFINED SRCDIR)
    set(SRCDIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add system include directories using CMAKE_SYSROOT
message(STATUS "CMAKE_SYSROOT is ${CMAKE_SYSROOT}")
include_directories(
    SYSTEM
    ${CMAKE_SYSROOT}/usr/include/isi
    ${CMAKE_SYSROOT}/usr/include/vvcam
    ${CMAKE_SYSROOT}/usr/include/vvcam/isi
)

# # Find all .c source files in drv/*/source/ directories
# file(GLOB_RECURSE SOURCE_FILES "${SRCDIR}/*.c")

# just add the main c file to SOURCES
list(APPEND SOURCE_FILES "${SRCDIR}/flir_boson.c")

# set target name to flir_boson
set(TARGET_NAME "flir_boson")

# Create shared library target
add_library(${TARGET_NAME} SHARED ${SOURCE_FILES})

# Set output name to .drv extension
set_target_properties(${TARGET_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".drv"
    OUTPUT_NAME ${TARGET_NAME}
    POSITION_INDEPENDENT_CODE ON
)

install(TARGETS ${TARGET_NAME}
        PUBLIC_HEADER   DESTINATION ${CMAKE_INSTALL_PREFIX}/include/${module}
        ARCHIVE         DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/${module}
        LIBRARY         DESTINATION ${CMAKE_INSTALL_PREFIX}/${module}
        )

set(calib_file "boson_mono.xml")
install(FILES ${SRCDIR}/${calib_file} DESTINATION ${CMAKE_INSTALL_PREFIX}/)

set(dewarp_config "sensor_dwe_boson_config.json")
install(FILES ${SRCDIR}/${dewarp_config} DESTINATION ${CMAKE_INSTALL_PREFIX}/dewarp_config/)
