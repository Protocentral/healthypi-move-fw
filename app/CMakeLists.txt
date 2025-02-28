# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

macro(app_set_runner_args)
  if(CONFIG_BOARD_HEALTHYPI_MOVE_NRF5340_CPUAPP)
    # Use alternative QSPI configuration file when flashing Thingy53
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
                        src/sm/*.c
                        )    

target_sources(app PRIVATE ${app_sources})

zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/drivers)
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

# This places images in the external flash memory
#zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/hw_module.c LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/images/*.c LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/fonts/*.c LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/sm/*.c LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/*.c LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/data_module.c LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/max32664d_msbl.c LOCATION EXTFLASH_TEXT NOCOPY)

zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/screens/*.c LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(FILES ${WEST_TOPDIR}/application/drivers/sensor/max32664dmax32664d_bl.c LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(FILES ${WEST_TOPDIR}/application/drivers/sensor/max32664d/max32664d_msbl.c LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(FILES ${WEST_TOPDIR}/application/drivers/sensor/max32664c/*.* LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(FILES ${WEST_TOPDIR}/application/drivers/sensor/max32664d/*.* LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(FILES ${WEST_TOPDIR}/application/drivers/sensor/bmi323hpi/*.* LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(FILES $${WEST_TOPDIR}/application/drivers/sensor/max32664d/max32664d_bl.c LOCATION EXTFLASH_TEXT NOCOPY)
#zephyr_code_relocate(FILES $${WEST_TOPDIR}/application/drivers/sensor/max32664d/max32664d_msbl.h LOCATION EXTFLASH_TEXT NOCOPY)

zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__mgmt LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__transport LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__util LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__smp LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__grp__img_mgmt LOCATION EXTFLASH_TEXT NOCOPY)
zephyr_code_relocate(LIBRARY subsys__mgmt__mcumgr__grp__os_mgmt LOCATION EXTFLASH_TEXT NOCOPY)

zephyr_code_relocate(LIBRARY modules__cmsis-dsp LOCATION EXTFLASH_TEXT NOCOPY)