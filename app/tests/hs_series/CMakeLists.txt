# P3: series bucketing unit test (no hardware).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hs_series)

set(HS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
    src/main.c
    ${HS_SRC}/health/hpi_hs_series.c   # the unit under test
)
# hpi_hs_series.c is deliberately pure (stdint/stdbool only), so none of the
# store's filesystem/zbus/hw_module deps are linked in here.
target_include_directories(app PRIVATE ${HS_SRC})
