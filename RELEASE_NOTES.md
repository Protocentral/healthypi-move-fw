# HealthyPi Move Firmware Release Notes

---

## v2.0.1

**Release Date:** January 2026

This is a patch release with critical bug fixes for battery monitoring, ECG/HRV lead handling, recording module, and BPT calibration.

### Bug Fixes

#### Battery Monitoring
- **Fixed**: Battery percentage going backwards (increasing while discharging)
  - Root cause: Current sign convention mismatch between Zephyr sensor API and nrf_fuel_gauge library
  - Zephyr uses negative current for discharging; nrf_fuel_gauge expects negative for charging
  - Added current sign inversion in `battery_fuel_gauge_update()` to match the existing fix in `battery_fuel_gauge_init()`

#### ECG/HRV Lead Detection
- **Fixed**: ECG stabilization not completing when leads connected on screen entry
  - Added `set_hrv_eval_active()` call when leads connect during HRV evaluation
- **Fixed**: Timer resets during stabilization phase causing measurement restarts
  - Added guards to prevent timer resets during stabilization
- **Fixed**: Lead-off detection not handled correctly during stabilization exit state
- **Fixed**: Lead placement timeout not clearing when leads reconnected
- **Improved**: Timer display logic with cleaner stabilization vs recording state handling

#### Recording Module
- **Fixed**: GSR recording buffer not cleared properly, causing data corruption
- **Fixed**: GSR recording state not reset after recording completion
- **Fixed**: GSR recording not stopping correctly when cancelled during active recording
  - Added 30-second buffer clear and state reset on cancellation
- **Added**: Proper GSR recording start trigger in recording module

#### Blood Pressure (BPT)
- **Fixed**: Calibration progress screen cancel semaphore handling
- **Improved**: BPT screen structure and layout

---

## v2.0.0 Release Notes

**Release Date:** December 2025

This is a major release introducing Heart Rate Variability (HRV) measurement, enhanced GSR stress analysis, and numerous bug fixes and UI improvements.

## New Features

### Heart Rate Variability (HRV) Measurement
- **Complete HRV Analysis**: New 60-second HRV evaluation using ECG-based R-R interval detection
- **Time-Domain Metrics**: SDNN, RMSSD, pNN50, and mean RR interval calculations
- **Frequency-Domain Analysis**: LF/HF ratio computation using FFT-based power spectral density
  - Low Frequency (LF) band: 0.04-0.15 Hz - reflects sympathetic and parasympathetic activity
  - High Frequency (HF) band: 0.15-0.4 Hz - reflects parasympathetic (vagal) activity
- **Autonomic Balance Indicator**: Visual balance bar showing sympathetic/parasympathetic dominance
- **New HRV Screens**:
  - `scr_hrv.c` - Main HRV dashboard with LF/HF ratio display and balance visualization
  - `scr_hrv_eval_progress.c` - Real-time ECG display during HRV measurement
  - `scr_hrv_complete.c` - Results screen with comprehensive HRV metrics
- **HRV Data Storage**: Results saved to device storage with trend tracking

### GSR/EDA Enhancements
- **New GSR Algorithms Module** (`gsr_algos.c`): Complete signal processing pipeline
  - Raw-to-microsiemens conversion with proper MAX30001 calibration
  - Tonic (SCL) and phasic (SCR) component separation
  - SCR peak detection for stress response counting
- **GSR Recording**: Full GSR session recording with data export capability
- **Improved GSR Screen**: Displays tonic level (SCL in µS) and SCR rate (peaks/min)

### Recording Module Improvements
- **Multi-Signal Recording**: Support for recording ECG, PPG, and GSR data simultaneously
- **New Recording Screen** (`scr_recording.c`): Visual feedback during active recordings
- **Enhanced File Management**: Improved LittleFS integration for reliable data storage

### Timeout Screens
- **SpO2 Timeout Screen** (`scr_spo2_timeout.c`): User feedback when SpO2 measurement times out
- **BPT Timeout Screen** (`scr_timeout.c`): Generic timeout handling for blood pressure measurements

## Bug Fixes

### ECG/HRV Lead Handling
- **Fixed**: Timer not restarting after lead reconnection during HRV/ECG measurement
  - Added `ecg_restabilization_pending` flag to preserve recording state during re-stabilization
  - Corrected lead reconnection detection logic (removed incorrect `timer_was_running` check)
- **Fixed**: Lead placement timeout continuing to run after leads reconnected
- **Fixed**: ECG plotting sometimes not starting even when leads were connected on screen entry
  - Reset `chart_ecg_update` flag when ECG screen is loaded

### SpO2 Measurement
- **Fixed**: Progress indicator jumping to invalid values (e.g., 178%)
  - Added clamping to 0-100% range in `hpi_disp_spo2_update_progress()`
  - Initialized `ppg_sensor_sample` struct to zero to prevent garbage values
- **Fixed**: Finger SpO2 waveform showing junk data at measurement start
  - Added 50-sample warmup period before plotting
  - Initialized chart with baseline value using `lv_chart_set_all_value()`
- **Fixed**: High-water mark protection prevents progress regression during measurement

### PPG Autoscaling
- **Improved**: PPG waveform autoscaling with hysteresis to reduce jitter
- **Added**: Periodic forced rescale to catch gradual amplitude changes
- **Added**: Clip detection to force rescale when signal approaches bounds
- **Added**: `hpi_ppg_autoscale_reset()` function for clean state on screen entry

### Blood Pressure (BPT)
- **Fixed**: Calibration Required screen crash due to unavailable font
  - Changed from `lv_font_montserrat_48` to `lv_font_montserrat_24`
- **Fixed**: Warning icon placement on Calibration Required screen
  - Reordered layout: warning icon now appears above the title

### State Machine Improvements
- **Fixed**: Stack overflow in ECG state machine thread
  - Increased thread stack from 1024 to 4096 bytes for file write operations
- **Improved**: Lead on/off semaphore handling in display thread
- **Added**: Clear lead placement timeout when leads reconnect mid-measurement

## UI/UX Improvements

### Modern Style System
- Consistent styling across all screens using shared style definitions
- Large numeric fonts for primary metrics (heart rate, SpO2, LF/HF ratio)
- Improved color coding for different measurement types:
  - Purple theme for HRV
  - Teal theme for GSR
  - Orange theme for ECG
  - Blue theme for SpO2/BPT

### Screen Layout Optimizations
- Optimized layouts for 390x390 circular AMOLED display
- AMOLED power efficiency with pure black backgrounds
- Consistent button placement and sizing across screens

## Technical Changes

### Dependencies
- **CMSIS-DSP**: Enabled for FFT-based frequency domain HRV analysis
- ARM math library integration for efficient DSP operations

### Configuration Changes
- HRV measurement duration: 60 seconds
- ECG measurement duration: 30 seconds
- FFT size: 64-point for reliable LF/HF computation with shorter recordings
- Interpolation sampling frequency: 4 Hz for R-R interval resampling

## Known Issues

- HRV accuracy depends on stable lead contact for the full 60-second measurement period
- GSR stress level display temporarily hidden pending further validation