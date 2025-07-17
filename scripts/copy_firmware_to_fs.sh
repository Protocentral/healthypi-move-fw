#!/bin/bash

# Script to copy firmware binary files to littlefs filesystem during build
# This makes the firmware files available for the updater to read

FIRMWARE_SOURCE_DIR="$1/firmware"
FIRMWARE_DEST_DIR="$2/firmware"

if [ ! -d "$FIRMWARE_SOURCE_DIR" ]; then
    echo "Error: Firmware source directory not found: $FIRMWARE_SOURCE_DIR"
    exit 1
fi

echo "Copying firmware files to littlefs..."
mkdir -p "$FIRMWARE_DEST_DIR"

# Copy firmware binary files
if [ -f "$FIRMWARE_SOURCE_DIR/max32664c_30_13_31.bin" ]; then
    cp "$FIRMWARE_SOURCE_DIR/max32664c_30_13_31.bin" "$FIRMWARE_DEST_DIR/"
    echo "Copied max32664c_30_13_31.bin ($(stat -f%z "$FIRMWARE_SOURCE_DIR/max32664c_30_13_31.bin" 2>/dev/null || stat -c%s "$FIRMWARE_SOURCE_DIR/max32664c_30_13_31.bin" 2>/dev/null) bytes)"
fi

if [ -f "$FIRMWARE_SOURCE_DIR/max32664d_40_6_0.bin" ]; then
    cp "$FIRMWARE_SOURCE_DIR/max32664d_40_6_0.bin" "$FIRMWARE_DEST_DIR/"
    echo "Copied max32664d_40_6_0.bin ($(stat -f%z "$FIRMWARE_SOURCE_DIR/max32664d_40_6_0.bin" 2>/dev/null || stat -c%s "$FIRMWARE_SOURCE_DIR/max32664d_40_6_0.bin" 2>/dev/null) bytes)"
fi

echo "Firmware files copied successfully to $FIRMWARE_DEST_DIR"
