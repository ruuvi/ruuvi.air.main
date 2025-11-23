# RuuviAir RGB LED Controller

This is a graphical user interface (GUI) application designed to test and control the 3-color (RGB) LED on a RuuviAir device over a Bluetooth Low Energy (BLE) connection.

The application provides controls to scan for nearby RuuviAir devices, establish a connection, and adjust the current and PWM values for the red, green, and blue LEDs independently.

## Prerequisites

  * **Python 3.10** or newer.
  * A **RuuviAir** device with firmware that supports the RGB LED control via the MCUMgr shell.
  * A computer with a **Bluetooth adapter** compatible with your operating system or
    **nRF52840-DK** or **nRF52840-dongle** with `hci_usb` firmware.

### Using `hci_usb` Firmware

Pre-built firmware for the nRF52840-DK and nRF52840-dongle are automatically built and
included in the `firmware` directory of this repository.

#### Flashing `hci_usb` Firmware for nRF52840-DK

```shell
nrfjprog -f nrf52 --recover
nrfjprog -f nrf52 --program firmware/hci_usb_nrf52840dk.hex --chiperase --verify --hardreset
```

or
```shell
nrfutil device recover
nrfutil device program \
  --firmware firmware/hci_usb_nrf52840dk.hex \
  --traits jlink \
  --options chip_erase_mode=ERASE_ALL,reset=RESET_HARD,verify=VERIFY_READ
```

#### Flashing `hci_usb` Firmware for nRF52840-dongle

```shell
nrfutil device program --firmware firmware//hci_usb_nrf52840dongle.zip --traits nordicDfu
```

#### Building `hci_usb` Firmware for nRF52840-DK
If you need to build the `hci_usb` firmware yourself, follow these steps:
```shell
cd ~/ncs/2.9.2/zephyr/samples/bluetooth/hci_usb
west build -b nrf52840dk/nrf52840 -d build_nrf52840dk .
```

#### Workaround for the problem with `hci_usb` firmware on Ubuntu Linux
If you encounter issues with the `hci_usb` firmware on Ubuntu Linux,
which manifests itself as the error message: "Error: can't scan: Invalid HCI Command Parameters"
when trying to scan for devices,
you may need to use the following workaround:
```shell
sudo hcitool cmd 0x3f 0x06 0x0 0x0 0x1 0x2 0x21 0xAD
```
The solution is described here: [Unable to use mcumgr cli over ble from linux(debian11 and ubuntu18/16) running on virtual machine](https://devzone.nordicsemi.com/f/nordic-q-a/98337/unable-to-use-mcumgr-cli-over-ble-from-linux-debian11-and-ubuntu18-16-running-on-virtual-machine)

#### Building `hci_usb` Firmware for nRF52840-dongle
If you need to build the `hci_usb` firmware yourself, follow these steps:
```shell
cd ~/ncs/v2.9.2/zephyr/samples/bluetooth/hci_usb
west build -b nrf52840dongle/nrf52840 -d build_nrf52840dongle .
nrfutil pkg generate --hw-version 52 --sd-req=0x00 \
        --application build_nrf52840dongle/hci_usb/zephyr/zephyr.hex \
        --application-version 1 build_nrf52840dongle/hci_usb.zip
```

## Setup & Running the Application

This project includes convenient scripts to automate the setup of a Python virtual environment and
the installation of required packages.

### On Windows

1.  Ensure all project files (`ruuvi_air_rgb_ctrl.py`, `ruuvi_air_rgb_ctrl.bat`, `requirements.txt`) are
    in the same directory.
2.  Simply double-click the **`ruuvi_air_rgb_ctrl.bat`** file.

The script will automatically:

  * Create a Python virtual environment in a `venv` folder if it doesn't already exist.
  * Install the necessary packages (`bleak`, `smpclient`).
  * Launch the application.

### On Linux / macOS

1.  Ensure all project files (`ruuvi_air_rgb_ctrl.py`, `ruuvi_air_rgb_ctrl.sh`, `requirements.txt`) are
    in the same directory.
2.  Open a terminal in that directory.
3.  Make the script executable by running the following command once:
    ```shell
    chmod +x ruuvi_air_rgb_ctrl.sh
    ```
4.  Run the script:
    ```shell
    ./ruuvi_air_rgb_ctrl.sh
    ```

The script will perform the same setup steps as the Windows version and then launch the application.

## How to Use the Application

1.  **Scan for Devices**: Click the **"Start Scan"** button. The application will search for nearby BLE devices for 7 seconds.
2.  **Select Device**: A list of discovered devices will appear. Click on your RuuviAir device in the list. The **"Connect"** button will become enabled.
3.  **Connect**: Click the **"Connect"** button. The status indicator will turn green upon a successful connection.
4.  **Control LEDs**: Use the sliders in the "RGB LED Controls" section to adjust the **Current** and **PWM** values for each color channel. The changes are sent to the device instantly.
5.  **Disconnect**: When you are finished, click the **"Disconnect"** button.
