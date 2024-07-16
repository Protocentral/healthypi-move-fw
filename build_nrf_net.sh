source ~/zephyrproject/zephyr/zephyr-env.sh
west build -p auto -b healthypi_fit_nrf5340_cpunet -d build_net cpunet_src -- -DBOARD_ROOT=../ -DCONF_FILE=prj_net.conf
