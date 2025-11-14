#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev_name=RuuviAir-A2 \
  --build_mode=release \
  --build_dir_suffix=df_e0_f0 \
  --extra_cflags="-DRUUVI_DATA_FORMAT_E0_F0=1"  
  "$@"
