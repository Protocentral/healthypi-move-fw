License Information
===================

HealthyPi Move is open hardware and open source. Different parts of the project
are released under different licenses, summarised below.

| Part | License |
|---|---|
| Hardware (schematics, PCB, mechanical) | **CERN-OHL-P v2** (permissive) |
| Software / firmware | **MIT** (with exceptions — see below) |
| Documentation | **CC BY-SA 4.0** |

---

Hardware
--------

**All hardware is released under the
[CERN Open Hardware Licence Version 2 - Permissive (CERN-OHL-P v2)](https://cern-ohl.web.cern.ch/).**

You may use, study, modify, manufacture and distribute the hardware designs and
products derived from them, including commercially, without the obligation to
release your modifications. Attribution and retention of notices are required.
The designs are provided without warranty, to the extent permitted by law.


Software
--------

**The firmware in this repository is primarily released under the
[MIT License](http://opensource.org/licenses/MIT), Copyright (c) 2019-2025
Protocentral Electronics.** The full MIT text is in [`LICENSE`](LICENSE).

**Not every file is MIT.** This repository also contains:

| License | Where | Notes |
|---|---|---|
| Apache-2.0 | Zephyr/NCS build glue, board files, some drivers | Standard Zephyr ecosystem licensing |
| BSD-3-Clause | `drivers/sensor/bmi323hpi/bmi323_hpi.h` | Bosch Sensortec SensorAPI header |
| **LicenseRef-Nordic-5-Clause** | 4 Nordic-derived config/data files | ⚠️ **Not OSI-approved open source.** Redistribution is permitted **only for use with Nordic Semiconductor devices.** |
| SIL OFL 1.1 | Bundled typefaces in `app/src/ui/fonts/` | Inter, JetBrains Mono, Jost, Manrope, Orbitron, Rubik, Saira, Train One |
| Apache-2.0 | Material Symbols icon font | Google LLC |

Every source file carries an `SPDX-License-Identifier`. Full license texts are in
[`LICENSES/`](LICENSES/). Third-party and derived components — including code
forked from Zephyr and from other open-source projects — are itemised with
attribution in [`THIRD_PARTY.md`](THIRD_PARTY.md).

**Vendor sensor-hub firmware is not distributed here.** The MAX32664C/D hub
firmware images (`.msbl`) are proprietary to Analog Devices and are **not**
included in this repository. See `app/src/max32664_updater/` for how they are
provisioned to the device filesystem.


Documentation
-------------

**Documentation and design assets are released under
[Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)](http://creativecommons.org/licenses/by-sa/4.0/).**

You are free to share and adapt the material for any purpose, including
commercially, provided you give appropriate credit, indicate changes, and
distribute your contributions under the same license.
