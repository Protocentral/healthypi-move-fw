source ~/zephyrproject/zephyr/zephyr-env.sh
west build -p auto -b healthypi_fit_nrf5340_cpuapp -d build . -- -DBOARD_ROOT=. -DCONF_FILE=prj_noboot.conf
