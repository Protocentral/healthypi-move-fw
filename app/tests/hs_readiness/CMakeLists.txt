# H6: readiness score unit test (no hardware).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hs_readiness)

set(HS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# hpi_hs_readiness.h is a header-only pure function (stdint only), so nothing from
# the store's filesystem/zbus/hw deps is linked in — just the test.
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${HS_SRC})
