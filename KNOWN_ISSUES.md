# Known Issues

Current known limitations and open issues in this firmware. Please check here before
filing a bug. Last updated: 2026-07-20.

---

## Firmware update (DFU / OTA)

### ⚠️ Updating a device running older firmware is not yet verified

Flashing over SWD works, and OTA works when the device is **already running a build from
this branch**. What has **not** been validated is the field-upgrade path: a device running
**older/shipped firmware** receiving an OTA package built from this branch.

- The application-core flash layout (`slot0`/`slot1`) and the signing key are unchanged
  from earlier releases, so the **app-core image** is expected to apply — but this has not
  been confirmed on a device running the older firmware.
- The **network-core image** path on older firmware is the bigger unknown: if the older
  build used a different image count or manifest indexing, the net-core upload may be
  rejected (or hit the image-index issue below).

**Recommendation:** until this is validated on a bench unit, prefer **application-core-only
OTA** when updating devices in the field, and use SWD for the network core.

### Mismatched DFU packages can crash the update session

If the DFU package does not match the running firmware's image layout — for example an
**older `dfu_application.zip` whose manifest tags the second image as index 2** — the
device receives an out-of-range image index and the mcumgr image handler asserts:

```
ASSERTION FAIL [slot >= 0 && slot < (2 << 1)] "Impossible slot number"
FATAL: reason=4 thread=mcumgr smp
```

The device **recovers automatically** (the fault handler reboots it) but the update aborts.
This firmware supports exactly two images: **0 = application core**, **1 = network core**.

**Workaround:** always use the `dfu_application.zip` produced by the same build you are
deploying. Rebuilding the package resolves it.

*Note for production builds:* this is an assertion (`CONFIG_ASSERT=y`, a development
setting). A release build should consider disabling asserts so a malformed request returns
an SMP error instead of resetting the device.

### Harmless log line during an update

```
mcumgr_img_grp: Failed to open flash area ID 3: -2
```

This is **expected and benign**. Flash area 3 is the network-core image's *primary* slot,
which on the nRF5340 is a deliberately disabled RAM placeholder — it is not a real readable
area. It appears only when a client queries image state; the upload path does not use it.
No action needed.

---

## Real-time clock

The RV-8263-C8 RTC was recently switched from an out-of-tree driver to the **Zephyr
in-tree driver** (`CONFIG_RTC_RV8263`, compatible `microcrystal,rv-8263-c8`). The build is
verified, but **setting/reading the time has not yet been re-validated on hardware** after
the swap. If you see incorrect date/time, please report it with the output of the MCUmgr
`os datetime` command.

---

## Security — firmware signing keys

The signing keys committed under `app/keys/` are **development keys and are public**. Any
image signed with them will be accepted by any device that trusts them, so they provide **no
firmware-authenticity guarantee**.

This is a deliberate trade-off so anyone can build and update a stock device. **If you are
building a product on this platform, generate your own keys** — see
*"These are development keys — use your own for production"* in the [README](README.md).

---

## Sensor / measurement

- **Blood-pressure (BPT)** requires the external finger sensor and a 3-point calibration
  before estimates are available. Calibration data is stored on-device.
- **MAX32664C/D sensor-hub firmware (`.msbl`) is not distributed** in this repository (it
  is proprietary to Analog Devices). The hub updater expects the images on the device
  filesystem under `/lfs/sys/`. Without them the hubs run whatever firmware is already
  flashed.
- Derived metrics (readiness/recovery, baselines) need several days of wear before they
  report meaningful values; until then they return a sentinel.
