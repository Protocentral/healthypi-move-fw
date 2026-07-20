# Third-Party Components

Components in this repository that are **not** original Protocentral work, or that
carry a license other than MIT. Full license texts are in [`LICENSES/`](LICENSES/).

Most of the firmware is Protocentral-authored and MIT-licensed (see [`LICENSE`](LICENSE)).
The drivers for the MAX30001, MAX30208, MAX32664C/D, SH8601 display and CHSC5816 touch
controller are original Protocentral work.

---

## ⚠️ Nordic Semiconductor — LicenseRef-Nordic-5-Clause (NOT OSI open source)

**Redistribution of these files is permitted only for use with Nordic Semiconductor
devices.** They are Nordic-derived configuration/data, retained because the firmware
targets the nRF5340.

| File | Purpose | Required? |
|---|---|---|
| `app/Kconfig.sysbuild` | Declares the app's sysbuild options; auto-discovered by sysbuild | **Yes** |
| `app/src/battery_profile_200.inc` | nPM1300 fuel-gauge model for the 200 mAh cell; `#include`d by `app/src/battery_module.c` | **Yes** |
| `app/sysbuild/ipc_radio/prj.conf` | Net-core (BLE controller) image configuration | **Yes** |
| `app/sysbuild/mcuboot/boards/healthypi_move_nrf5340_cpuapp_minimal.conf` | MCUboot board config (keeps the bootloader inside its 64 KB partition) | **Yes** |
| `app/linker_arm_extxip.ld` | QSPI external-XIP linker script | **No — unused.** Both enabling options are commented out (`SB_CONFIG_QSPI_XIP_SPLIT_IMAGE`, `CONFIG_CUSTOM_LINKER_SCRIPT`). Safe to delete. |

License text: [`LICENSES/LicenseRef-Nordic-5-Clause.txt`](LICENSES/LicenseRef-Nordic-5-Clause.txt)

## Bosch Sensortec — BSD-3-Clause

| File | Origin |
|---|---|
| `drivers/sensor/bmi323hpi/bmi323_hpi.h` | Bosch Sensortec SensorAPI `bmi3_defs.h` v2.1.0 (register/constant definitions). Copyright (c) 2023 Bosch Sensortec GmbH. |

License text: [`LICENSES/BSD-3-Clause.txt`](LICENSES/BSD-3-Clause.txt)

## Zephyr / nRF Connect SDK ecosystem — Apache-2.0

| File(s) | Copyright |
|---|---|
| `CMakeLists.txt`, `Kconfig`, `zephyr/module.yml`, `boards/protocentral/healthypi_move/*` | Zephyr build glue — Nordic Semiconductor ASA, Linaro and Zephyr contributors |
| `drivers/sensor/bmi323hpi/{bmi323_hpi.c,CMakeLists.txt,Kconfig}` | Derived from the Zephyr mainline BMI323 driver — © 2023 Trackunit Corporation |
| `drivers/input/Kconfig` | © 2023 Google LLC (Zephyr input subsystem) |

> **RTC:** the RV-8263-C8 real-time clock uses the **Zephyr in-tree driver**
> (`CONFIG_RTC_RV8263`, compatible `microcrystal,rv-8263-c8`), fetched via `west` and not
> vendored here — so it carries no attribution burden on this repository. The previous
> out-of-tree copy under `drivers/rtc/` was removed in favour of upstream.

License text: [`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt)

## Fonts

Bundled typefaces in `app/src/ui/fonts/ttf/`, plus the generated LVGL glyph tables in
`app/src/ui/fonts/lvgl/*.c` which embed outline data from them.

| Typeface(s) | License |
|---|---|
| Inter, JetBrains Mono, Jost, Manrope, Orbitron, Rubik, Saira, Train One | SIL Open Font License 1.1 — [`app/src/ui/fonts/OFL.txt`](app/src/ui/fonts/OFL.txt) |
| Material Symbols Outlined | Apache-2.0, © Google LLC — [`app/src/ui/fonts/LICENSE-Apache-2.0.txt`](app/src/ui/fonts/LICENSE-Apache-2.0.txt) |

## Vendor firmware — NOT distributed

The MAX32664C/D sensor-hub firmware images (`.msbl`) are **proprietary to Analog
Devices/Maxim** and are **not included** in this repository. They are loaded from the
device filesystem at runtime (`/lfs/sys/*.msbl`); see `app/src/max32664_updater/`.

## Fetched, not vendored

Fetched at build time via `west.yml` (`sdk-nrf` v3.4.0) and not redistributed here:
nRF Connect SDK, Zephyr RTOS, MCUboot, LVGL, CMSIS-DSP.
