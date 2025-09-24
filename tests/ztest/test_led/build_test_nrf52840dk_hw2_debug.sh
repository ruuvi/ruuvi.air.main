#!/bin/bash

set -x -e # Print commands and their arguments and Exit immediately if a command exits with a non-zero status.

west build -b nrf52840dk_ruuviair@2/nrf52840 . -d build_nrf52840dk -DBOARD_ROOT=$PWD/../../.. \
    -DEXTRA_DTC_OVERLAY_FILE="$PWD/../../../dts_nrf52840dk.overlay;$PWD/../../../dts_nrf52840dk_hw2.overlay;$PWD/../../../dts_common.overlay"

