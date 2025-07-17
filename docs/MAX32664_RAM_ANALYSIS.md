# MAX32664 Updater - RAM Usage Analysis

## Summary

This document analyzes the RAM usage of both the original array-based MAX32664 updater and the new filesystem-based implementation.

## Original Array-Based Updater (max32664_updater.c)

### Static/Global Variables:
```c
uint8_t max32664_fw_init_vector[11] = {0};          // 11 bytes
uint8_t max32664_fw_auth_vector[16] = {0};          // 16 bytes
static uint8_t tmp_wr_buf[8][1026];                 // 8,208 bytes (8×1026)
```

### Local Variables (per function call):
```c
// m_read_bl_ver()
uint8_t rd_buf[4];                                  // 4 bytes
uint8_t wr_buf[2];                                  // 2 bytes

// m_write_set_num_pages()
uint8_t rd_buf[1];                                  // 1 byte
uint8_t wr_buf[3];                                  // 3 bytes

// m_write_init_vector()
uint8_t rd_buf[1];                                  // 1 byte
uint8_t wr_buf[13];                                 // 13 bytes

// m_write_auth_vector()
uint8_t rd_buf[1];                                  // 1 byte
uint8_t wr_buf[18];                                 // 18 bytes

// m_fw_write_page()
uint8_t rd_buf[1];                                  // 1 byte
uint8_t cmd_wr_buf[2];                              // 2 bytes
struct i2c_msg max32664_i2c_msgs[9];                // ~180 bytes (9×~20 bytes)
```

### Firmware Arrays (Flash, not RAM):
```c
extern const uint8_t max32664c_msbl[238112];        // 238,112 bytes in FLASH
extern const uint8_t max32664d_40_6_0[172448];      // 172,448 bytes in FLASH
```

**Total Original RAM Usage: ~8,480 bytes**

## New Filesystem-Based Updater (max32664_updater_fs.c)

### Static/Global Variables:
```c
uint8_t max32664_fw_init_vector[11] = {0};          // 11 bytes
uint8_t max32664_fw_auth_vector[16] = {0};          // 16 bytes
static uint8_t fw_page_buffer[8208];                // 8,208 bytes (MAX32664C_FW_UPDATE_WRITE_SIZE)
static uint8_t fw_header_buffer[0x100];             // 256 bytes
static uint8_t tmp_wr_buf[8][1026];                 // 8,208 bytes (8×1026)
```

### Local Variables (per function call):
```c
// max32664_load_fw_from_fs()
struct fs_file_t fw_file;                           // ~64-128 bytes (filesystem state)

// m_fw_write_page_from_buffer()
uint8_t rd_buf[1];                                  // 1 byte
uint8_t cmd_wr_buf[2];                              // 2 bytes
struct i2c_msg max32664_i2c_msgs[9];                // ~180 bytes (9×~20 bytes)

// Other functions same as original...
```

### Firmware Arrays (Conditional - Flash, not RAM):
```c
#ifdef CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS
extern const uint8_t max32664c_msbl[238112];        // 238,112 bytes in FLASH (optional)
extern const uint8_t max32664d_40_6_0[172448];      // 172,448 bytes in FLASH (optional)
#endif
```

**Total Filesystem-Based RAM Usage: ~16,960 bytes**

## Detailed Comparison

| Component | Original (bytes) | Filesystem (bytes) | Difference |
|-----------|------------------|-------------------|------------|
| **Static Buffers** |
| Init/Auth vectors | 27 | 27 | 0 |
| I2C write buffer | 8,208 | 8,208 | 0 |
| Page buffer | 0 | 8,208 | +8,208 |
| Header buffer | 0 | 256 | +256 |
| **Local Variables** |
| Function locals | ~245 | ~245 | 0 |
| File handle | 0 | ~100 | +100 |
| **Total RAM** | **~8,480** | **~16,960** | **+8,480** |
| **Flash Arrays** |
| Firmware data | 410,560 | 0-410,560* | -410,560* |

*Depends on CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS setting

## RAM Usage Analysis

### Why More RAM in Filesystem Version?

1. **Page Buffer**: The filesystem version needs a dedicated page buffer (8,208 bytes) to read data from the filesystem before writing to I2C.

2. **Header Buffer**: An additional 256-byte buffer is used to read and parse the firmware header separately.

3. **File Handle**: Filesystem operations require maintaining file state (~100 bytes).

### Memory Efficiency Considerations

The RAM increase is justified because:

1. **Flash Savings**: Saves 400KB+ of precious main flash memory
2. **External Storage**: Firmware now stored in external flash (littlefs partition)
3. **Page-by-Page**: Still processes data in manageable chunks
4. **No Full Load**: Never loads entire firmware into RAM

## Memory Optimization Opportunities

### 1. Shared Buffer Approach
The page buffer and header buffer could potentially be shared since they're not used simultaneously:

```c
// Instead of separate buffers:
static uint8_t fw_page_buffer[8208];
static uint8_t fw_header_buffer[256];

// Use a union or reuse the page buffer:
static uint8_t fw_buffer[8208];  // Use first 256 bytes for header
```

**Potential RAM Savings: 256 bytes**

### 2. Stack-Based Header Reading
For the small header (256 bytes), we could use stack allocation instead of static:

```c
uint8_t header_buf[256];  // Stack allocation in function
```

**Potential RAM Savings: 256 bytes**

### 3. Reduced I2C Write Buffers
The tmp_wr_buf could potentially be optimized by using the page buffer more efficiently.

## Platform Impact

### For Resource-Constrained Devices:
- **RAM**: Additional ~8.5KB RAM usage
- **Flash**: Saves 400KB+ main flash
- **External Flash**: Uses littlefs partition

### Recommendations:
1. **For devices with limited RAM (<32KB)**: Consider shared buffer optimization
2. **For devices with ample RAM (>64KB)**: Current implementation is fine
3. **For maximum flash savings**: Disable fallback arrays (`CONFIG_MAX32664_UPDATER_INCLUDE_FALLBACK_ARRAYS=n`)

## Conclusion

The filesystem-based updater uses approximately **8.5KB more RAM** but saves **400KB+ of main flash memory**. This is an excellent trade-off for most embedded systems where:

- Main flash is typically more constrained than RAM
- External flash (via littlefs) is larger and less expensive
- The firmware update operation is infrequent
- The additional RAM usage is temporary (only during updates)

The implementation maintains the same page-by-page processing approach, ensuring predictable and bounded memory usage during the update process.
