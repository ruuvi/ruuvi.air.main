#!/bin/bash

nrfutil toolchain-manager launch twister -- -c \
	--board-root $PWD/boards -x=BOARD_ROOT=$PWD -p ruuvi_ruuviair/nrf52840 \
	-x=EXTRA_DTC_OVERLAY_FILE="$PWD/dts_ruuviair.overlay;$PWD/dts_common.overlay" \
	-v --device-testing --device-serial /dev/ttyUSB0 --west-flash --inline-logs \
        -T .
# -T tests/ztest/test_led
# -T tests/ztest/test_dsp
# -T tests/ztest/test_spl_calc
