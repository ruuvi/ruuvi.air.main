#!/bin/bash

nrfutil toolchain-manager launch twister -- -c \
    -p nrf52840dk_ruuviair/nrf52840 \
    --board-root $PWD/boards \
    -x=BOARD_ROOT=$PWD  \
    -x=EXTRA_DTC_OVERLAY_FILE="$PWD/dts_nrf52840dk.overlay;$PWD/dts_nrf52840dk_hw2.overlay;$PWD/dts_common.overlay" \
    -v --inline-logs \
    -T .
# -T tests/ztest/test_led
# -T tests/ztest/test_dsp
# -T tests/ztest/test_spl_calc
