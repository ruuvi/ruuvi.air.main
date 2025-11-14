
# RuuviAir LED Calibration

This guide describes the process for calibrating the main LED on
RuuviAir devices. The calibration ensures accurate color reproduction
by measuring the relationship between LED current and actual light output
using an optical sensor.

## Overview

The calibration process involves:
1. Setting up test hardware with two RuuviAir devices and a BLE bridge
2. Collecting optical measurements while cycling through LED colors and intensities
3. Processing the measurement data to generate calibration tables
4. Updating the firmware with the new calibration data

## Hardware Requirements

To perform LED calibration, you need the following hardware:

1. **Test Device**: A RuuviAir device in its case with special firmware that
   cycles through colors (None/Red/Green/Blue) as LED current increases from 0 to 255.
   - Use firmware from `./firmware/fw_led_cycle_calibrate/`

2. **Measurement Device**: A RuuviAir device without casing, running firmware
   to measure light intensity using the OPT4060 sensor and transmit data via BLE NUS.
   - Use firmware from `./firmware/fw_opt4060_measurements/`

3. **BLE Bridge**: nRF52840-dongle or nRF52840-DK with
   [ruuvi_ble_nus](https://github.com/ruuvi/ruuvi.air.ble_nus) firmware.

4. **Test Setup**:
   - Position the measurement device so its OPT4060 sensor is directly opposite
     the light guide outlet of the test device
   - Place both devices in a light-proof enclosure to prevent external light interference


## Data Collection

### Setup
After connecting the BLE bridge device to your computer, a new serial port should appear:
- Linux: `/dev/ttyACM4` (or similar)
- Windows: `COM5` (or similar)

**Important**: Do not collect logs from a virtual machine, as it may suspend and cause measurement data loss.

### Collecting Measurements

Use the data collection script from the ruuvi_ble_nus project:

**Linux:**
```bash
python3 ../ruuvi_ble_nus/scripts/ruuvi_ble_nus_read_raw_log.py --port /dev/ttyACM4 --mac_addr E1:9D:F3:4F:8A:A5
```

**Windows:**
```bash
python ../ruuvi_ble_nus/scripts/ruuvi_ble_nus_read_raw_log.py --port COM5 --mac_addr E1:9D:F3:4F:8A:A5
```

Replace `E1:9D:F3:4F:8A:A5` with the MAC address of your measurement device.

### Log Format
The collected data will be in the following format:
```text
[00:01:33.855,560] sensors: [main/0x20007568/0] OPT4060: R=384.000000, G=1184.000000, B=62.400002, L=5.727600
[00:01:34.355,712] sensors: [main/0x20007568/0] OPT4060: R=nan, G=1192.000000, B=67.599998, L=5.719000
```

## Calibration Procedure

### Step 1: Initial Setup
1. Power on only the measurement device (RuuviAir with OPT4060 firmware)
2. Start the data collection script
3. Verify that brightness values are reading zero (no external light)

### Step 2: Data Collection
1. Power on the test device (RuuviAir with LED cycling firmware)
2. Allow the system to run for 45 minutes to collect a complete calibration dataset
3. Stop data collection (press `Ctrl+C`)

### Step 3: Process Calibration Data
1. Rename the generated log file from `YYYY-MM-DDTHH-mm-ss_raw_log.log` to `stage_0_led_test.log`
2. Run the processing script:
   ```bash
   ./process_calibration_log.sh
   ```
3. The script will automatically update `../src/led_calibration.c` and `../src/led_calibration.h`

### Step 4: Update RGB Control Module
Manually copy the content from `stage_5_result.csv` to the `BRIGHTNESS_TO_CURRENT_CSV` variable in `../ruuvi_air_rgb_ctrl/ruuvi_air_rgb_ctrl.py`

## Files Generated During Processing

- `stage_0_led_test.log` - Raw measurement data
- `stage_1_calibration_data.csv` - Initial processed data
- `stage_2_filtered_data.csv` - Filtered measurements
- `stage_3_joined_rgbw.csv` - RGB data combined with white channel
- `stage_4_with_approximated_red.csv` - Red channel approximation applied
- `stage_5_result.csv` - Final calibration table

## Requirements

- Python 3.10 with packages listed in `requirements.txt`
- Access to serial port for BLE bridge device
- Light-proof enclosure for testing setup

## Troubleshooting

- If measurements show non-zero values before starting the test, ensure the setup is completely light-tight
- If data collection fails, verify the MAC address and serial port are correct
- For processing script issues, ensure Python virtual environment is properly set up

## Related Documentation

- [ruuvi_ble_nus](https://github.com/ruuvi/ruuvi.air.ble_nus) - BLE bridge firmware
- Firmware files are located in `./firmware/` directory
