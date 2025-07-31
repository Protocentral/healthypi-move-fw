# HealthyPi Move Firmware - AI Agent Instructions

## Project Overview
HealthyPi Move is a Zephyr RTOS-based firmware for a wearable vital signs monitoring device (ECG, PPG, SpO₂, temperature, blood pressure) built on nRF5340 dual-core SoC. The firmware uses a modular architecture with Zephyr's ZBus for inter-component communication.

## Architecture & Key Patterns

### Core Architecture
- **Hardware Abstraction**: `hw_module.c` is the central hardware coordinator, initializing all sensors and peripherals
- **Communication**: ZBus channels in `hpi_zbus_channels.c` enable decoupled communication between modules
- **State Management**: State machine framework (SMF) in `sm/smf_display.c` manages UI states and display logic
- **Data Flow**: Sensor data → ZBus channels → Multiple listeners (display, trends, BLE, system)

### ZBus Communication Pattern
```c
// Define channels for typed message passing
ZBUS_CHAN_DEFINE(hr_chan, struct hpi_hr_t, NULL, NULL, 
                 ZBUS_OBSERVERS(disp_hr_lis, trend_hr_lis, sys_hr_lis), 
                 ZBUS_MSG_INIT(0));

// Create listeners for each module
ZBUS_LISTENER_DEFINE(disp_hr_lis, disp_hr_listener);
```

### Module Structure
- **hw_module**: Hardware initialization and sensor coordination
- **ble_module**: Bluetooth communication for data streaming
- **fs_module**: LittleFS filesystem for data logging and firmware storage
- **ui/**: LVGL-based user interface with state machine
- **sm/**: State machine implementations for display and system logic
- **max32664_updater**: Over-the-air firmware updates for biometric sensor hub

## Development Workflows

### Building & Flashing
```bash
# Standard build for HealthyPi Move
west build -b healthypi_move_nrf5340_cpuapp app

# Flash with custom QSPI config
west flash

# Alternative builds
./scripts/build_nrf.sh  # Development build
./scripts/flash_merged.sh  # Flash merged hex
```

### Key Build Configurations
- **Sysbuild**: Multi-core build with MCUboot bootloader, secure boot for network core
- **Board**: `healthypi_move_nrf5340_cpuapp` (custom board definition in `boards/protocentral/`)
- **QSPI**: External flash configuration via `qspi_w25.ini`

### Project-Specific Conventions

#### Memory Management
- Shared buffers for firmware updates: `shared_rw_buffer[SHARED_BUFFER_SIZE]`
- Optimized message queues with reduced sizes: `K_MSGQ_DEFINE(q_plot_ecg_bioz, ..., 32, 1)`
- Static allocation preferred over dynamic for embedded constraints

#### Error Handling
- Systematic logging with module-specific log levels
- Fatal error handler triggers cold reboot: `k_sys_fatal_error_handler()`
- Filesystem operations always check return values and provide fallback paths

#### Sensor Integration
- Each sensor has dedicated driver in `drivers/sensor/`
- Hardware initialization follows device tree configuration
- Sensor data flows through ZBus channels to multiple consumers

#### Configuration System
- User settings persistence via `hpi_settings_persistence.c`
- Runtime configuration through `hpi_user_settings_api.c`
- Zephyr settings subsystem for NVS storage

## Critical Integration Points

### Dual-Core Architecture (nRF5340)
- **Application Core**: Main firmware, UI, sensors, data processing
- **Network Core**: Bluetooth stack (configured via sysbuild)
- **IPC**: Inter-processor communication for BLE data transfer

### External Dependencies
- **Zephyr RTOS**: v3.0-branch via west manifest
- **Nordic SDK**: nRF Connect SDK for Bluetooth and power management
- **LVGL**: UI framework for touch display
- **LittleFS**: Filesystem for data logging and firmware storage

### Filesystem Layout
```
/lfs/
├── max32664c_msbl.msbl     # Sensor hub firmware (C variant)
├── max32664d_40_6_0.msbl   # Sensor hub firmware (D variant)
└── [data logs]             # Sensor data and user settings
```

### Power Management
- **Sleep States**: Automatic sleep with configurable timeout via user settings
- **Battery Monitoring**: NPM1300 PMIC integration with fuel gauge
- **Power Optimization**: Runtime PM for sensors, display backlight control

## Common Development Tasks

### Adding New Sensors
1. Create driver in `drivers/sensor/[sensor_name]/`
2. Add device tree binding in `dts/bindings/`
3. Initialize in `hw_module.c`
4. Create ZBus channel in `hpi_zbus_channels.c`
5. Add UI components in `ui/screens/`

### Firmware Updates
- MAX32664 sensor hub updates via `max32664_updater/`
- Files stored in LittleFS at `/lfs/` paths
- Streaming updates to conserve RAM

### UI Development
- LVGL components in `ui/components/`
- Screen definitions in `ui/screens/`
- State management via SMF in `sm/smf_display.c`
- Custom fonts and images in respective subdirectories

### Debugging
- RTT logging enabled by default
- Module-specific log levels configurable via Kconfig
- Power profiling via `sys_poweroff()` calls
- Filesystem debugging via directory listing on errors

## Key Files for Context
- `app/src/main.c`: Entry point and fatal error handling
- `app/src/hw_module.c`: Hardware coordination (1147 lines)
- `app/src/hpi_zbus_channels.c`: Inter-module communication definitions
- `app/src/sm/smf_display.c`: UI state machine (1103 lines)
- `app/CMakeLists.txt`: Build configuration with conditional compilation
- `west.yml`: Dependency management and SDK version pinning
