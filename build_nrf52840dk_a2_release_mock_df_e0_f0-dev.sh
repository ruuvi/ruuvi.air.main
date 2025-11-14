#!/bin/bash

exec ./build.sh \
  --board=nrf52840dk \
  --board_rev_name=RuuviAir-A2 \
  --build_mode=release \
  --build_dir_suffix=mock_df_e0_f0 \
  --extra_cflags="-DRUUVI_MOCK_MEASUREMENTS=1;-DRUUVI_DATA_FORMAT_E0_F0=1"
  "$@"
