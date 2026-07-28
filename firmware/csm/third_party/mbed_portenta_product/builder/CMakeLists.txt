cmake_minimum_required(VERSION 3.19.0 FATAL_ERROR)

if(NOT DEFINED MBED_PATH)
    message(FATAL_ERROR "MBED_PATH must point to the pinned Mbed OS source")
endif()
set(MBED_CONFIG_PATH "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "")
include(${MBED_PATH}/tools/cmake/app.cmake)

project(csm_mbed_product)

# GCC 7 on Windows expands response-file definitions again when it launches
# cc1. Materializing Mbed's generated definitions as a forced header keeps the
# command below CreateProcess limits and matches ArduinoCore-mbed's ABI model.
set(CSM_PRODUCT_CONFIG_DEFINITIONS ${MBED_CONFIG_DEFINITIONS})
set(MBED_CONFIG_DEFINITIONS CSM_PRODUCT_MBED_PROFILE=1)
set(CSM_PRODUCT_CONFIG_HEADER "${CMAKE_CURRENT_BINARY_DIR}/mbed_config.h")
file(WRITE "${CSM_PRODUCT_CONFIG_HEADER}"
    "#pragma once\n/* Generated from the pinned CSM Mbed configuration. */\n")
foreach(definition IN LISTS CSM_PRODUCT_CONFIG_DEFINITIONS)
    string(FIND "${definition}" "=" equals_index)
    if(equals_index EQUAL -1)
        set(definition_name "${definition}")
        set(definition_value "1")
    else()
        string(SUBSTRING "${definition}" 0 ${equals_index} definition_name)
        math(EXPR value_index "${equals_index} + 1")
        string(SUBSTRING "${definition}" ${value_index} -1 definition_value)
    endif()
    if(definition_name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        file(APPEND "${CSM_PRODUCT_CONFIG_HEADER}"
            "#define ${definition_name} ${definition_value}\n")
    endif()
endforeach()

add_subdirectory(${MBED_PATH} ${CMAKE_CURRENT_BINARY_DIR}/mbed-os)
target_compile_options(mbed-core INTERFACE
    -include "${CSM_PRODUCT_CONFIG_HEADER}")

add_executable(csm_mbed_product main.cpp)
target_link_libraries(csm_mbed_product
    mbed-os
    mbed-netsocket
    mbed-lwipstack
    mbed-emac
    mbed-wifi
    mbed-cy-psoc6-common-network
    mbed-cy-psoc6-whd
    mbed-portenta-whd
    mbed-portenta-whd-4343w-fw
    mbed-portenta-whd-4343w-nvram
    mbed-storage-blockdevice
    mbed-storage-qspif
    mbed-storage-filesystem
    mbed-storage-fat
)
mbed_set_post_build(csm_mbed_product)
