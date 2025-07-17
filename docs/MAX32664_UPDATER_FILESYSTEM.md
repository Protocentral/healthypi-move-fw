# MAX32664 Updater - Filesystem-Based Firmware Storage

## Overview

The MAX32664 updater has been modified to store firmware data in the littlefs filesystem instead of as arrays in flash memory. This significantly reduces flash memory usage while maintaining the same functionality.

## Changes Made

### 1. Array to Binary Conversion
- Created script `scripts/convert_array_to_bin.py` to convert C arrays to binary files
- Generated `app/firmware/max32664c_30_13_31.bin` (238,112 bytes)
- Generated `app/firmware/max32664d_40_6_0.bin` (172,448 bytes)

### 2. New Filesystem-Based Updater
- Created `lib/max32664_updater/max32664_updater_fs.c` - new updater implementation
- Reads firmware data page-by-page from littlefs instead of loading entire arrays
- Maintains the same I2C page-wise writing behavior as the original implementation

### 3. Configuration Options
- Added `CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS` Kconfig option
- When enabled, includes original arrays as fallback (uses more flash)
- When disabled, pure filesystem-based approach (saves maximum flash)

### 4. CMake Updates
- Modified `lib/max32664_updater/CMakeLists.txt` to use new updater
- Conditionally includes fallback arrays based on configuration
- Added notes about firmware file requirements

## Flash Memory Savings

### Before (Array-based):
- max32664c_30_13_31.c array: 238,112 bytes in flash
- max32664d_40_6_0.c array: 172,448 bytes in flash
- **Total: ~410,560 bytes in main flash**

### After (Filesystem-based):
- Firmware stored in external flash (littlefs partition)
- Main flash usage: Only the updater code (~8KB)
- **Savings: ~400KB+ of main flash memory**

## Usage

### Option 1: With Fallback Arrays (Recommended for first deployment)
1. Enable `CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS=y`
2. Build and flash the firmware
3. On first run, arrays will be copied to filesystem automatically
4. Optionally disable the config and rebuild to save flash

### Option 2: Pure Filesystem-based (Maximum flash savings)
1. Ensure `CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS=n`
2. Upload firmware files to device using MCU Manager:
   ```bash
   ./scripts/upload_firmware.sh
   ```
3. Build and flash the firmware

### Manual File Upload via MCU Manager
```bash
# Create firmware directory
mcumgr fs mkdir /lfs/firmware

# Upload firmware files
mcumgr fs upload app/firmware/max32664c_30_13_31.bin /lfs/firmware/max32664c_30_13_31.bin
mcumgr fs upload app/firmware/max32664d_40_6_0.bin /lfs/firmware/max32664d_40_6_0.bin
```

## File Locations

### Source Files:
- `lib/max32664_updater/max32664_updater_fs.c` - New filesystem-based updater
- `app/firmware/max32664c_30_13_31.bin` - Binary firmware for MAX32664C
- `app/firmware/max32664d_40_6_0.bin` - Binary firmware for MAX32664D

### Device Filesystem:
- `/lfs/firmware/max32664c_30_13_31.bin` - MAX32664C firmware on device
- `/lfs/firmware/max32664d_40_6_0.bin` - MAX32664D firmware on device

## Implementation Details

### Page-by-Page Reading
The new implementation:
1. Opens the firmware file from littlefs
2. Reads the header to get number of pages and vectors
3. Seeks to the firmware data start position
4. Reads exactly one page (8208 bytes) at a time
5. Writes the page directly to the MAX32664 via I2C
6. Repeats until all pages are written

This approach ensures:
- Minimal RAM usage (only one page buffered at a time)
- Same I2C communication pattern as the original
- No changes required to existing calling code

### Error Handling
- File not found errors are logged clearly
- Read/write errors are propagated to the caller
- Graceful fallback to arrays if configured

## Scripts

- `scripts/convert_array_to_bin.py` - Convert C arrays to binary files
- `scripts/upload_firmware.sh` - Upload firmware files via MCU Manager
- `scripts/copy_firmware_to_fs.sh` - Helper for build-time copying (unused in current implementation)

## Benefits

1. **Flash Memory Savings**: ~400KB+ saved in main flash
2. **External Storage**: Firmware stored in external flash (littlefs partition)
3. **Updateable**: Firmware files can be updated via MCU Manager without reflashing
4. **Backwards Compatible**: Same API and behavior as original updater
5. **Flexible**: Can be configured with or without fallback arrays
