# MAX32664 Updater - RAM Optimization Example

This file demonstrates how to reduce RAM usage in the filesystem-based updater by sharing buffers.

## Key Optimization: Shared Buffer

Instead of separate buffers:
```c
static uint8_t fw_page_buffer[8208];     // Page buffer
static uint8_t fw_header_buffer[256];    // Header buffer
```

Use a single shared buffer:
```c
static uint8_t fw_buffer[8208];          // Single buffer for both
#define fw_header_buffer fw_buffer       // Header uses first 256 bytes
#define HEADER_SIZE 0x100
```

## RAM Savings: 256 bytes

This optimization is safe because:
1. Header is read first (uses first 256 bytes of buffer)
2. Header data is extracted to separate variables immediately
3. Same buffer is then reused for page-by-page reading
4. No overlap between header and page operations

## Implementation Changes Required

1. Replace separate buffer declarations with shared buffer
2. Ensure header processing completes before page reading starts
3. Update buffer usage in functions accordingly

This optimization reduces RAM usage from ~16,960 bytes to ~16,704 bytes while maintaining all functionality.
