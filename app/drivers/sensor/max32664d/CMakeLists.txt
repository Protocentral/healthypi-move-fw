
zephyr_library()
zephyr_include_directories(.)
zephyr_library_sources(max32664d.c max32664d_bl.c)

zephyr_library_sources_ifdef(CONFIG_SENSOR_ASYNC_API max32664d_async.c max32664d_decoder.c)