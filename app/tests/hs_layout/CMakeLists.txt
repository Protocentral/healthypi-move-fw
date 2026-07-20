# HS-2 P2: seq <-> segment/offset arithmetic unit test (native_sim, no hardware).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hs_layout)

set(HS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${HS_SRC})
