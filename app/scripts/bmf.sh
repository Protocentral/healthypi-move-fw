rm -rf merged.hex
source ~/zephyrproject/zephyr/zephyr-env.sh
west build -p auto -b healthypi5_rp2040 --sysbuild . -- -DBOARD_ROOT=/Users/akw/Documents/GitHub/protocentral_healthypi5_zephyr
hexmerge.py -o merged.hex --no-start-addr build/mcuboot/zephyr/zephyr.hex build/protocentral_healthypi5_zephyr/zephyr/zephyr.signed.hex

/opt/homebrew/bin/openocd -s /Users/akw/Documents/GitHub/protocentral_healthypi5_zephyr/boards/arm/healthypi5_rp2040/support -s /Users/akw/zephyr-sdk-0.16.1/sysroots/arm64-pokysdk-linux/usr/share/openocd/scripts -f /Users/akw/Documents/GitHub/protocentral_healthypi5_zephyr/boards/arm/healthypi5_rp2040/support/openocd.cfg -c 'source [find interface/cmsis-dap.cfg]' -c 'transport select swd' -c 'source [find target/rp2040.cfg]' -c 'set_adapter_speed_if_not_set 20000' '-c init' '-c targets' -c 'reset init' -c 'flash write_image erase /Users/akw/Documents/GitHub/protocentral_healthypi5_zephyr/merged.hex' -c 'reset run' -c shutdown
