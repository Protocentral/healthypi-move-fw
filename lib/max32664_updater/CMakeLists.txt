
#add_library(max32664_updater STATIC "")
#target_sources(max32664_updater PRIVATE max32664_updater.c )
#get_target_property(include_dirs app INCLUDE_DIRECTORIES)
#target_link_libraries(max32664_updater PUBLIC zephyr_interface)
#add_dependencies(max32664_updater zephyr_generated_headers)


zephyr_library()
zephyr_include_directories(.)
# Use filesystem-based updater instead of array-based to save flash memory
zephyr_library_sources(max32664_updater_fs.c)

# Conditionally include fallback arrays if enabled
if(CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS)
    zephyr_library_sources(msbl/max32664c_30_13_31.c msbl/max32664d_40_6_0.c)
    message(STATUS "Including fallback firmware arrays for max32664_updater")
endif()

# Remove array files to save flash space - firmware is now loaded from littlefs
# zephyr_library_sources(max32664_updater.c msbl/max32664c_30_13_31.c msbl/max32664d_40_6_0.c)