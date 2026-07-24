# HS-2 P3: HRV accumulator unit test (no hardware).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hs_hrv)

set(HS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_sources(app PRIVATE src/main.c ${HS_SRC}/health/hpi_hs_hrv.c)
target_include_directories(app PRIVATE ${HS_SRC})
