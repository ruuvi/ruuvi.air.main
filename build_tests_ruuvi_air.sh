#!/bin/bash

nrfutil toolchain-manager launch twister -- -c \
    -p ruuvi_ruuviair/nrf52840 \
    --board-root $PWD/boards -x=BOARD_ROOT=$PWD  \
    -x=EXTRA_DTC_OVERLAY_FILE="$PWD/dts_common.overlay" \
    -v --inline-logs \
    -T .
