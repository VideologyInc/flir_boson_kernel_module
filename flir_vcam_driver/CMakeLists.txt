cmake_minimum_required(VERSION 3.10)
project(flir-vvcam-driver C)

# Required defines for ARM64 build
add_definitions(
    -DARM64
    -DHAL_ALTERA
    -DLINUX
    -DLIVE_SENSOR
    -DSUBDEV_V4L2
)

# Source directory (can be overridden)
if(NOT DEFINED SRCDIR)
    set(SRCDIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()


# Add system include directories using CMAKE_SYSROOT
message(STATUS "CMAKE_SYSROOT is ${CMAKE_SYSROOT}")
include_directories(
    SYSTEM
    ${CMAKE_SYSROOT}/usr/include/isi
    ${CMAKE_SYSROOT}/usr/include/vvcam
    ${CMAKE_SYSROOT}/usr/include/vvcam/isi
)

# Find all .c source files in drv/*/source/ directories
file(GLOB_RECURSE SOURCE_FILES "${SRCDIR}/*.c")

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