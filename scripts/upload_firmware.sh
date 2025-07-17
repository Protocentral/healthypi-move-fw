#!/bin/bash

# Script to upload firmware files to the device using MCU Manager
# This allows the filesystem-based updater to access the firmware

DEVICE_PORT="/dev/ttyACM0"  # Adjust as needed
FIRMWARE_DIR="app/firmware"

# Function to upload a file
upload_file() {
    local_file="$1"
    remote_path="$2"
    
    if [ ! -f "$local_file" ]; then
        echo "Error: Local file not found: $local_file"
        return 1
    fi
    
    echo "Uploading $local_file to $remote_path..."
    
    # Use mcumgr to upload the file
    # Adjust the connection parameters as needed for your setup
    mcumgr --conntype serial --connstring "dev=$DEVICE_PORT,baud=115200" fs upload "$local_file" "$remote_path"
    
    if [ $? -eq 0 ]; then
        echo "Successfully uploaded $local_file"
    else
        echo "Failed to upload $local_file"
        return 1
    fi
}

# Main script
echo "MAX32664 Firmware Upload Script"
echo "================================"

# Check if mcumgr is available
if ! command -v mcumgr &> /dev/null; then
    echo "Error: mcumgr is not installed or not in PATH"
    echo "Please install mcumgr first: https://github.com/apache/mynewt-mcumgr-cli"
    exit 1
fi

# Check if firmware directory exists
if [ ! -d "$FIRMWARE_DIR" ]; then
    echo "Error: Firmware directory not found: $FIRMWARE_DIR"
    echo "Please run this script from the project root directory"
    exit 1
fi

# Create firmware directory on device
echo "Creating firmware directory on device..."
mcumgr --conntype serial --connstring "dev=$DEVICE_PORT,baud=115200" fs mkdir /lfs/firmware

# Upload firmware files
if [ -f "$FIRMWARE_DIR/max32664c_30_13_31.bin" ]; then
    upload_file "$FIRMWARE_DIR/max32664c_30_13_31.bin" "/lfs/firmware/max32664c_30_13_31.bin"
fi

if [ -f "$FIRMWARE_DIR/max32664d_40_6_0.bin" ]; then
    upload_file "$FIRMWARE_DIR/max32664d_40_6_0.bin" "/lfs/firmware/max32664d_40_6_0.bin"
fi

echo "Firmware upload complete!"
echo ""
echo "You can now use the filesystem-based updater which will read"
echo "the firmware from littlefs instead of using flash memory."
