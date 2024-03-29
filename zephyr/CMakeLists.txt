# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

# find_package(Zephyr) in order to load application boilerplate:
# http://docs.zephyrproject.org/application/application.html
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Path to kvs library
get_filename_component(TOP_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
set(KVS_DIR "${TOP_DIR}/lib")
message(STATUS "KVS_DIR = ${KVS_DIR}")
assert_exists(KVS_DIR)

if(CONFIG_KVS)
zephyr_include_directories(
    ${KVS_DIR}/include
)
zephyr_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
zephyr_library_sources(
    ${KVS_DIR}/src/kvs.c
)

add_subdirectory(subsys/kvs)

endif()

add_subdirectory_ifdef(CONFIG_SETTINGS_KVS subsys/settings)