# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

macro(app_set_runner_args)
  if(CONFIG_BOARD_HEALTHYPI_MOVE_NRF5340_CPUAPP)
    # Use alternative QSPI configuration file 
    board_runner_args(nrfjprog "--qspiini=${CMAKE_CURRENT_SOURCE_DIR}/qspi_w25.ini")
  endif()
endmacro()

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
                        
target_sources(app PRIVATE ${app_sources})

if(CONFIG_MAX32664_UPDATER)
    add_subdirectory(lib/max32664_updater)
    target_include_directories(app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/max32664_updater)
endif()

zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/drivers)
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

# This places images in the external flash memory

#zephyr_code_relocate(FILES lib/max32664_updater/max32664_updater.c LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(FILES lib/max32664_updater/msbl/max32664c_30_13_31.c LOCATION EXTFLASH_RODATA NOCOPY)
#zephyr_code_relocate(FILES lib/max32664_updater/msbl/max32664d_40_6_0.c LOCATION EXTFLASH_RODATA NOCOPY)

#zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__mgmt LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__transport LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__util LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__smp LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__grp__img_mgmt LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__grp__os_mgmt LOCATION EXTFLASH_TEXT NOCOPY)

#zephyr_code_relocate(LIBRARY modules__cmsis-dsp LOCATION EXTFLASH_TEXT NOCOPY)