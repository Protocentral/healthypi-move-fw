<div align="center">
  
![HealthyPi Move Logo](docs/images/healthypi_move_logo.png)

</div>

# HealthyPi Move Firmware

[![CI Build](https://github.com/protocentral/healthypi-move-fw/actions/workflows/ci.yml/badge.svg)](https://github.com/protocentral/healthypi-move-fw/actions/workflows/ci.yml) 

This repository contains the firmware for the HealthyPi Move device. The firmware is based on the [Zephyr RTOS](https://www.zephyrproject.org/). 

HealthyPi Move is an open hardware device that lets you track all your vital signs to a high degree of accuracy. But it’s not just another smartwatch with a heart rate monitor. It is a complete vital signs monitoring and recording device on your wrist that can measure electrocardiogram (ECG), photoplethysmogram (PPG), SpO₂, blood pressure (finger-based), EDA/GSR, heart rate variability (HRV), respiration rate, and even body temperature.

![HealthyPi Move](docs/images/healthypi-move.jpg)

The hardware files for the HealthyPi Move are available at their own repository - [HealthyPi Move Hardare](https://github.com/Protocentral/healthypi-move-hw)

HealthyPi Move is now available for pre-order in the ongoing campaign on [Crowd Supply](https://www.crowdsupply.com/protocentral/healthypi-move)

## Repository Contents

* **/app** - Main application code for the HealthyPi Move device
* **/drivers** - Custom device drivers for onboard sensors (MAX30001, MAX32664C/D, BMI323, etc.)
* **/boards** - Board definition files and device tree configurations
* **/scripts** - Build and flash convenience scripts
* **/docs** - Documentation and design notes

## Features

- **Vital Signs Monitoring**: ECG, PPG, SpO₂, Heart Rate, HRV, Respiration Rate, Body Temperature
- **Blood Pressure**: Finger-based blood pressure estimation with calibration
- **Activity Tracking**: Step counting, activity recognition via 6-axis IMU
- **Galvanic Skin Response (GSR)**: Stress and EDA measurement
- **Data Logging**: Internal storage of trends and recordings on 112MB LittleFS filesystem
- **Bluetooth LE**: Real-time streaming and data synchronization
- **DFU Support**: Over-the-air firmware updates via MCUBoot
- **Power Management**: Battery monitoring and optimized sleep modes
- **LVGL UI**: Modern touch-enabled display interface

## Getting Started

### Prerequisites

You need to have the Zephyr development environment set up. The recommended approach is to use the [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nrf-connect-sdk) which includes Zephyr and all required tools.

**Recommended: Install nRF Connect SDK with VS Code**

Follow the official [nRF Connect SDK Installation Guide](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation/install_ncs.html):

1. **Install Visual Studio Code**
   - Download and install [VS Code](https://code.visualstudio.com/)

2. **Install nRF Connect for VS Code Extension**
   - Open VS Code
   - Go to Extensions (Ctrl+Shift+X / Cmd+Shift+X)
   - Search for "nRF Connect for VS Code"
   - Install the extension

3. **Install Toolchain via nRF Connect Extension**
   - Click the nRF Connect icon in VS Code sidebar
   - Go to "Welcome" view
   - Click "Install Toolchain"
   - Select the recommended version
   - The extension will automatically install:
     - nRF Connect SDK (includes Zephyr)
     - ARM GCC toolchain
     - CMake, Ninja, and other build tools
     - West meta-tool
     - Python dependencies

### Cloning the Repository

**⚠️ Important**: Do not use the "Code" → "Download ZIP" button, as it will not include the Zephyr submodules correctly.

Initialize the workspace using West:

```bash
# Initialize west workspace with this repository as the manifest
west init -m https://github.com/protocentral/healthypi-move-fw --mr main healthypi-move-workspace

# Navigate to the workspace
cd healthypi-move-workspace

# Fetch all Zephyr modules and dependencies (this may take several minutes)
west update
```

This will create the following structure:
```
healthypi-move-workspace/
├── healthypi-move-fw/        # This repository
├── zephyr/                   # Zephyr RTOS
├── modules/                  # Zephyr modules
├── bootloader/               # MCUBoot bootloader
└── tools/                    # Build tools
```

### Building the Firmware

#### Method 1: Visual Studio Code (Recommended)

1. Open VS Code
2. Ensure the **nRF Connect for VS Code** extension is installed (see Prerequisites)
3. Click on the nRF Connect icon in the sidebar
4. Click "Open an existing application"
5. Navigate to `healthypi-move-workspace/healthypi-move-fw/app`
6. Click "Add Build Configuration"
7. Select board: `healthypi_move_nrf5340_cpuapp`
8. Click the "Build" button in the Actions panel

The extension will automatically:
- Configure CMake with correct parameters
- Build the application firmware
- Build the bootloader (MCUBoot)
- Generate merged binaries and DFU packages

Build artifacts will be in `build/`:
- `build/zephyr/zephyr.hex` - Application firmware
- `build/zephyr/merged.hex` - Merged firmware (bootloader + app)
- `build/dfu_application.zip` - DFU package for OTA updates

#### Method 2: Command Line (Advanced)

```bash
# Build for HealthyPi Move board
west build -b healthypi_move_nrf5340_cpuapp healthypi-move-fw/app

# For a clean build (recommended after major changes)
west build -b healthypi_move_nrf5340_cpuapp healthypi-move-fw/app --pristine

# Build with board root specification (alternative)
west build healthypi-move-fw/app --board healthypi_move_nrf5340_cpuapp -- -DBOARD_ROOT=healthypi-move-fw
```

### Flashing the Firmware

#### Method 1: VS Code nRF Connect Extension (Recommended)

1. Connect your J-Link, nRF52840 DK, or other compatible programmer to the HealthyPi Move SWD port
2. In VS Code, open the nRF Connect extension sidebar
3. In the "Actions" panel, click the "Flash" button
4. The extension will automatically:
   - Detect your programmer
   - Program the QSPI flash configuration (on first flash)
   - Flash the merged firmware
   - Reset the device

#### Method 2: Command Line (Advanced)

```bash
# Flash via connected programmer (auto-detects J-Link, etc.)
west flash

# Flash only the application (faster during development)
west flash --softreset

# Flash with specific runner (if needed)
west flash --runner nrfjprog
```

### Verifying the Build

After flashing, the device should:
1. Show the boot splash screen
2. Initialize all sensors
3. Display the home screen with current time
4. Be discoverable via Bluetooth as "healthypi move"

Check logs via UART (115200 baud, 8N1) on the debug port.

## Development Workflow

### Configuration

- **Application Config**: `app/prj.conf` - Main Kconfig options
- **Board Overlays**: `app/overlay-*.conf` - Board-specific configurations  
- **Partition Manager**: `app/pm_static.yml` - Flash partition layout
- **Sysbuild Config**: `app/sysbuild.conf` - Bootloader and multi-image settings

Key configuration options:
```
CONFIG_HPI_GSR_SCREEN=y              # Enable GSR feature
CONFIG_HPI_RECORDING_MODULE=y        # Enable multi-signal recording
CONFIG_HEAP_MEM_POOL_SIZE=24576      # Heap size
CONFIG_DEBUG_INFO=y                  # Enable debug symbols
```

### Directory Structure

```
app/
├── src/                      # Application source code
│   ├── *_module.c           # Hardware/feature modules
│   ├── sm/                  # State machines (Zephyr SMF)
│   └── ui/                  # LVGL user interface
│       ├── screens/         # Individual screen implementations
│       └── components/      # Reusable UI components
├── include/                 # Public headers
├── keys/                    # Signing keys for secure boot
├── build/                   # Build output directory
└── tests/                   # Unit tests

drivers/sensor/              # Custom sensor drivers
├── max30001/               # ECG/BioZ sensor
├── max32664c/              # PPG sensor (variant C)
├── max32664d/              # PPG sensor (variant D)
├── max30208/               # Temperature sensor
└── bmi323hpi/              # 6-axis IMU

boards/protocentral/healthypi_move/  # Board support files
├── healthypi_move_nrf5340_cpuapp.dts
├── healthypi_move_nrf5340_cpunet.dts
└── *.yaml                  # Board metadata
```

## Bluetooth LE API

The HealthyPi Move exposes various Bluetooth LE services and characteristics for custom application development. The device supports standard BLE GATT services as well as custom services for sensor data streaming.

### BLE Device Configuration

- **Device Name**: `healthypi move` (configurable via `CONFIG_BT_DEVICE_NAME`)
- **Appearance**: 833 (Health/Fitness device)
- **Security**: Supports pairing with passkey display and bonding
- **MTU**: 247 bytes (configurable via `CONFIG_BT_L2CAP_TX_MTU`)

### Standard BLE Services

| Service | UUID | Description | Characteristics |
|---------|------|-------------|-----------------|
| **Heart Rate Service (HRS)** | `0x180D` | Standard BLE Heart Rate Service | Heart Rate Measurement (0x2A37) - Notify, Read |
| **Battery Service (BAS)** | `0x180F` | Standard BLE Battery Service | Battery Level (0x2A19) - Notify, Read |
| **Device Information Service (DIS)** | `0x180A` | Device information and version | Manufacturer, Model, Firmware Revision |
| **Pulse Oximeter Service** | `0x1822` | Standard BLE Pulse Oximeter Service | PLX Spot-Check Measurement (0x2A5E) - Notify, Read, Encrypted |
| **Health Thermometer Service** | `0x1809` | Standard BLE Temperature Service | Temperature Measurement (0x2A1C) - Notify, Read, Encrypted |

### Custom BLE Services

#### 1. ECG/GSR Service (Streaming)
**Service UUID**: `00001122-0000-1000-8000-00805f9b34fb`

**Note**: This is a streaming service. Data streaming starts automatically when a client subscribes to notifications (enables CCCD) and stops automatically when unsubscribed.

| Characteristic | UUID | Properties | Data Format | Description |
|----------------|------|------------|-------------|-------------|
| **ECG Data** | `00001424-0000-1000-8000-00805f9b34fb` | Notify, Read | Array of int32_t (little-endian, 4 bytes per sample) | Real-time ECG waveform data. Multiple samples per notification. Streaming starts on subscription. |
| **GSR Data** | `babe4a4c-7789-11ed-a1eb-0242ac120002` | Notify, Read | Array of int32_t (little-endian, 4 bytes per sample) | Galvanic Skin Response (EDA) data. Multiple samples per notification. Streaming starts on subscription. |

#### 2. PPG Service (Streaming)
**Service UUID**: `cd5c7491-4448-7db8-ae4c-d1da8cba36d0`

**Note**: This is a streaming service. Data streaming starts automatically when a client subscribes to notifications (enables CCCD) and stops automatically when unsubscribed.

| Characteristic | UUID | Properties | Data Format | Description |
|----------------|------|------------|-------------|-------------|
| **PPG Wrist** | `cd5c1525-4448-7db8-ae4c-d1da8cba36d0` | Notify, Read | Array of uint32_t (little-endian, 4 bytes per sample) | Wrist PPG raw data (Green LED channel). Multiple samples per notification. Streaming starts on subscription. |
| **PPG Finger** | `cd5ca86f-4448-7db8-ae4c-d1da8cba36d0` | Notify, Read | Array of uint32_t (little-endian, 4 bytes per sample) | Finger PPG raw data (IR LED channel). Multiple samples per notification. Streaming starts on subscription. |

#### 3. Command Service
**Service UUID**: `01bf7492-970f-8d96-d44d-9023c47faddc`

| Characteristic | UUID | Properties | Data Format | Description |
|----------------|------|------------|-------------|-------------|
| **Command TX** | `01bf1528-970f-8d96-d44d-9023c47faddc` | Write, Write Without Response, Read, Authenticated | Command packets (see protocol below) | Send commands to device (time sync, calibration, logs, recordings) |
| **Command RX** | `01bf1527-970f-8d96-d44d-9023c47faddc` | Notify, Write Without Response, Read, Authenticated | Response packets (see protocol below) | Receive responses and data from device |

### Command Protocol

Commands are sent via the Command TX characteristic using the following packet format:

```
[SOF1] [SOF2] [LEN_LSB] [LEN_MSB] [PKT_TYPE] [PAYLOAD...] [STOP1] [STOP2]
```

- **SOF1/SOF2**: Start of frame markers (`0x0A`, `0xFA`)
- **LEN**: 16-bit packet length (little-endian)
- **PKT_TYPE**: Packet type (0x01 = Command, 0x02 = Data, 0x03 = Status, etc.)
- **PAYLOAD**: Command-specific data
- **STOP1/STOP2**: End of frame markers (`0x00`, `0x0B`)

#### Supported Commands

| Command ID | Name | Arguments | Description |
|------------|------|-----------|-------------|
| `0x40` | GET_DEVICE_STATUS | None | Get current device status |
| `0x41` | SET_DEVICE_TIME | Timestamp (unix epoch) | Synchronize device time |
| `0x42` | DEVICE_RESET | None | Reset device |
| `0x50` | LOG_GET_INDEX | Log type (uint8) | Get list of stored logs |
| `0x51` | LOG_GET_FILE | Session ID (uint16) | Retrieve log file data |
| `0x52` | LOG_DELETE | Session ID (uint16) | Delete specific log |
| `0x53` | LOG_WIPE_ALL | None | Delete all logs |
| `0x54` | LOG_GET_COUNT | Log type (uint8) | Get count of logs |
| `0x60` | BPT_SEL_CAL_MODE | None | Enter blood pressure calibration mode |
| `0x61` | START_BPT_CAL_START | Systolic (uint8), Diastolic (uint8) | Start BP calibration with reference values |
| `0x62` | BPT_EXIT_CAL_MODE | None | Exit BP calibration mode |
| `0x30` | RECORDING_COUNT | Recording type (uint8) | Get count of recordings |
| `0x31` | RECORDING_INDEX | Recording type (uint8) | Get list of recordings |
| `0x32` | RECORDING_FETCH_FILE | Recording type (uint8) | Retrieve recording file |
| `0x33` | RECORDING_DELETE | Recording type (uint8) | Delete recording |
| `0x34` | RECORDING_WIPE_ALL | None | Delete all recordings |

### Data Formats

#### Heart Rate (HRS)
- **Format**: Standard BLE Heart Rate Measurement format (uint16, BPM)
- **Update Rate**: Variable, based on detection

#### SpO₂ (Pulse Oximeter)
- **Format**: Standard BLE PLX Spot-Check Measurement format
- **Update Rate**: Variable, based on measurement completion

#### Temperature
- **Format**: Standard BLE Temperature Measurement format (IEEE-11073 FLOAT)
- **Update Rate**: Periodic updates

#### ECG Data
- **Format**: Array of signed 32-bit integers (int32_t)
- **Byte Order**: Little-endian (LSB first)
- **Encoding**: 4 bytes per sample: `[byte0, byte1, byte2, byte3]`
- **Sample Rate**: Typically 125-128 Hz
- **Samples per Packet**: Variable (typically 8-16 samples)

#### PPG Data (Wrist & Finger)
- **Format**: Array of unsigned 32-bit integers (uint32_t)
- **Byte Order**: Little-endian (LSB first)
- **Encoding**: 4 bytes per sample: `[byte0, byte1, byte2, byte3]`
- **Sample Rate**: Typically 128 Hz
- **Samples per Packet**: Variable (typically 8-16 samples)

#### GSR Data
- **Format**: Array of signed 32-bit integers (int32_t)
- **Byte Order**: Little-endian (LSB first)
- **Encoding**: 4 bytes per sample
- **Sample Rate**: Variable based on configuration

### Example Usage

#### Subscribing to ECG Data Streaming

1. **Connect** to the HealthyPi Move device using its advertised name: "healthypi move"
2. **Discover** the ECG/GSR Service using UUID: `00001122-0000-1000-8000-00805f9b34fb`
3. **Locate** the ECG Data characteristic: `00001424-0000-1000-8000-00805f9b34fb`
4. **Enable notifications** by writing `0x0001` to the characteristic's CCCD (Client Characteristic Configuration Descriptor)
5. **Receive notifications** - Each notification contains multiple ECG samples as an array of int32_t values
6. **Parse the data**:
   - Read 4 bytes at a time (little-endian)
   - Convert to signed 32-bit integer
   - Repeat for each sample in the notification
7. **Stop streaming** by writing `0x0000` to the CCCD when done

**Example Data Parsing** (pseudo-code):
```
notification_data = [0x10, 0x27, 0x00, 0x00, 0x20, 0x27, 0x00, 0x00, ...]  // Raw bytes

num_samples = length(notification_data) / 4
for i = 0 to num_samples-1:
    byte0 = notification_data[i*4 + 0]
    byte1 = notification_data[i*4 + 1]
    byte2 = notification_data[i*4 + 2]
    byte3 = notification_data[i*4 + 3]
    
    // Combine bytes into int32 (little-endian)
    sample = byte0 | (byte1 << 8) | (byte2 << 16) | (byte3 << 24)
    
    // Handle sign extension if needed for your platform
    if (sample > 2147483647):  // 0x7FFFFFFF
        sample = sample - 4294967296  // Convert to signed
    
    process_ecg_sample(sample)
```

#### Setting Device Time

1. **Connect** to the device
2. **Pair** if not already paired (required for authenticated characteristics)
3. **Locate** the Command TX characteristic: `01bf1528-970f-8d96-d44d-9023c47faddc`
4. **Build command packet**:
   ```
   [0x0A] [0xFA]                    // Start of frame
   [LEN_LSB] [LEN_MSB]              // Packet length (little-endian)
   [0x01]                            // Packet type (Command)
   [0x41]                            // Command ID (SET_DEVICE_TIME)
   [timestamp bytes]                 // Unix timestamp (32-bit, little-endian)
   [0x00] [0x0B]                    // End of frame
   ```
5. **Write** the complete packet to the Command TX characteristic
6. **Subscribe** to Command RX characteristic to receive response (if needed)

### Notes

- **Streaming services** (ECG/GSR and PPG) automatically start streaming data when a client subscribes to notifications and stop when unsubscribed - no additional commands are required
- All custom characteristics support Client Characteristic Configuration Descriptor (CCCD) for enabling/disabling notifications
- Encrypted characteristics require pairing before access
- The device uses LE Secure Connections (Security Level 2) with passkey authentication
- Maximum notification payload is limited by MTU (default 247 bytes)
- Battery level updates automatically and can be subscribed to for notifications

## License Information

![License](license_mark.svg)

This product is open source! Both, our hardware and software are open source and licensed under the following licenses:

Hardware
---------

**All hardware is released under the [CERN-OHL-P v2](https://ohwr.org/cern_ohl_p_v2.txt)** license.

Copyright CERN 2020.

This source describes Open Hardware and is licensed under the CERN-OHL-P v2.

You may redistribute and modify this documentation and make products
using it under the terms of the CERN-OHL-P v2 (https:/cern.ch/cern-ohl).
This documentation is distributed WITHOUT ANY EXPRESS OR IMPLIED
WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN-OHL-P v2
for applicable conditions

Software
--------

**All software is released under the MIT License(http://opensource.org/licenses/MIT).**

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Documentation
-------------
**All documentation is released under [Creative Commons Share-alike 4.0 International](http://creativecommons.org/licenses/by-sa/4.0/).**
![CC-BY-SA-4.0](https://i.creativecommons.org/l/by-sa/4.0/88x31.png)

You are free to:

* Share — copy and redistribute the material in any medium or format
* Adapt — remix, transform, and build upon the material for any purpose, even commercially.
The licensor cannot revoke these freedoms as long as you follow the license terms.

Under the following terms:

* Attribution — You must give appropriate credit, provide a link to the license, and indicate if changes were made. You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.
* ShareAlike — If you remix, transform, or build upon the material, you must distribute your contributions under the same license as the original.

Please check [*LICENSE.md*](LICENSE.md) for detailed license descriptions.