
zephyr_library()
zephyr_include_directories(.)
zephyr_library_sources(maxm86146.c) 

#max32664_bl.c)

zephyr_library_sources_ifdef(CONFIG_SENSOR_ASYNC_API maxm86146_async.c maxm86146_decoder.c)