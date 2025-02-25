source ~/zephyrproject/zephyr/zephyr-env.sh
west build -t menuconfig -b healthypi5_rp2040 -d build_rp2040  -- -DBOARD_ROOT=.
