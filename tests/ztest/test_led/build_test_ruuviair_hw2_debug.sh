#!/bin/bash

set -x -e # Print commands and their arguments and Exit immediately if a command exits with a non-zero status.

west build -b ruuvi_ruuviair@2/nrf52840 . -d build_ruuviair -DBOARD_ROOT=$PWD/../../.. \
    -DEXTRA_DTC_OVERLAY_FILE="$PWD/../../../dts_ruuviair.overlay;$PWD/../../../dts_ruuviair_hw2.overlay;$PWD/../../../dts_common.overlay"

