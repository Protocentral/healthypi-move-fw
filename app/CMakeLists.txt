# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(healthypi_move)

FILE(GLOB app_sources   src/*.c 
                        src/images/*.c 
                        src/ui/*.c 
                        src/ui/components/*.c
                        src/ui/images/*.c
                        src/ui/fonts/*.c
                        src/ui/screens/*.c
                        src/ui/screens/*.c
                        src/ui/hp_ui_common.c
                        src/sm/*.c)  

FILE(GLOB image_sources src/ui/images/*.c
                        src/ui/fonts/*.c)
                        
target_sources(app PRIVATE ${app_sources})

#add_subdirectory(lib/max32664_updater)
#target_link_libraries(app PUBLIC max32664_updater)
#target_include_directories((app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/max32664_updater))

zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/drivers)
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

# This places images in the external flash memory