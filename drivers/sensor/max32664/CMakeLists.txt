
zephyr_library()
zephyr_include_directories(.)
zephyr_library_sources(max32664.c)

zephyr_library_sources_ifdef(CONFIG_SENSOR_ASYNC_API max32664_async.c max32664_decoder.c)