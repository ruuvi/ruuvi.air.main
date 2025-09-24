#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev=2 \
  --build_mode=release \
  --build_dir_suffix=df_e0_f0 \
  --extra_cflags="-DRUUVI_DATA_FORMAT_E0_F0=1"  
  "$@"
