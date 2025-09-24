#!/bin/bash

exec ./build.sh \
  --board=nrf52840dk \
  --board_rev=1 \
  --build_mode=debug \
  --build_dir_suffix=mock \
  --extra_cflags="-DRUUVI_MOCK_MEASUREMENTS=1"
  "$@"
