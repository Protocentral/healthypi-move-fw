# HS-2 P1: epoch aggregator unit test (native_sim, no hardware).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hs_epoch)

set(HS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
    src/main.c
    ${HS_SRC}/health/hpi_hs_epoch.c   # the unit under test
)
# hpi_hs_record() is STUBBED in main.c, so the rest of the store (filesystem,
# zbus, hw_module) is not linked in at all.
target_include_directories(app PRIVATE ${HS_SRC})
