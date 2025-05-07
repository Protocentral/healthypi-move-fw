
#add_library(max32664_updater STATIC "")
#target_sources(max32664_updater PRIVATE max32664_updater.c )
#get_target_property(include_dirs app INCLUDE_DIRECTORIES)
#target_link_libraries(max32664_updater PUBLIC zephyr_interface)
#add_dependencies(max32664_updater zephyr_generated_headers)


zephyr_library()
zephyr_include_directories(.)
zephyr_library_sources(max32664_updater.c max32664c_30_13_31.c max32664d_40_6_0.c)