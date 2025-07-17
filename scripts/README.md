# Scripts Directory

This directory contains utility scripts for the MAX32664 firmware updater.

## Scripts Overview

### `convert_array_to_bin.py`
Converts C array files to binary files for filesystem storage.

**Usage:**
```bash
python3 convert_array_to_bin.py <input_c_file> <output_bin_file>
```

**Example:**
```bash
python3 convert_array_to_bin.py lib/max32664_updater/msbl/max32664c_30_13_31.c app/firmware/max32664c_30_13_31.bin
```

### `verify_firmware.py`
Verifies that the binary files match the original C arrays.

**Usage:**
```bash
python3 verify_firmware.py
```

### `upload_firmware.sh`
Uploads firmware binary files to the device using MCU Manager.

**Prerequisites:**
- MCU Manager CLI installed
- Device connected via serial port
- Device firmware with MCU Manager support

**Usage:**
```bash
./upload_firmware.sh
```

**Note:** Adjust the `DEVICE_PORT` variable in the script to match your device.

### `copy_firmware_to_fs.sh`
Helper script for copying firmware files during build (currently unused).

**Usage:**
```bash
./copy_firmware_to_fs.sh <source_dir> <dest_dir>
```

## Workflow

1. **Convert arrays to binary files:**
   ```bash
   python3 scripts/convert_array_to_bin.py lib/max32664_updater/msbl/max32664c_30_13_31.c app/firmware/max32664c_30_13_31.bin
   python3 scripts/convert_array_to_bin.py lib/max32664_updater/msbl/max32664d_40_6_0.c app/firmware/max32664d_40_6_0.bin
   ```

2. **Verify conversion:**
   ```bash
   python3 scripts/verify_firmware.py
   ```

3. **Upload to device:**
   ```bash
   ./scripts/upload_firmware.sh
   ```

## Requirements

- Python 3.x
- MCU Manager CLI (for upload_firmware.sh)
- Bash shell (for shell scripts)
