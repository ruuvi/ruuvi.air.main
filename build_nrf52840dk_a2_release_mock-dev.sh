#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
cd $SCRIPT_DIR

exec ./build.sh \
  --board=nrf52840dk \
  --board_rev_name=RuuviAir-A2 \
  --build_mode=release \
  --build_dir_suffix=mock \
  --extra_cflags="-DRUUVI_MOCK_MEASUREMENTS=1"
  "$@"
