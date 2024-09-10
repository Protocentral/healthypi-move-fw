# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Workaround for child image not working
set(hci_ipc_CONFIG_LOG n)
set(hci_ipc_CONFIG_SERIAL n)
#set(mcuboot_CONFIG_BOOT_SIGNATURE_KEY_FILE \"${CMAKE_CURRENT_SOURCE_DIR}/custom_key_dir/custom_priv.pem\")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(healthypi_move)

FILE(GLOB app_sources   src/*.c 
                        src/images/*.c 
                        src/ui/*.c 
                        src/ui/components/*.c
                        src/ui/images/*.c
                        src/ui/fonts/*.c
                        src/ui/screens/*.c)    
                        
target_sources(app PRIVATE ${app_sources})

zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/drivers)
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)


set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)