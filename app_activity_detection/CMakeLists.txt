cmake_minimum_required(VERSION 3.20.0)
list(APPEND ZEPHYR_EXTRA_MODULES
     ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/lib/tflite-micro)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_activity_detection)

target_sources(app PRIVATE
    src/main.c
    src/boot.c
    src/ui.c
    src/home_screen.c
    src/touch.c
    src/storage.c
    src/acc_recorder.c
    model/activity_model.cpp 
    src/gravity_filter.c
    src/ml_engine.cpp 
    src/activity.c 
)
target_include_directories(app PRIVATE src model)
zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/drivers)
# Driver headers (max32664c.h, max32664d.h, display_sh8601.h ...) may live in the
# base app's include dir; our own headers are app_*.h so nothing collides.
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/../app/include)
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../app/include)
endif()