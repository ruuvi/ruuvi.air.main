#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev_name=RuuviAir-A3 \
  --build_mode=debug \
  --extra_conf=prj_fw_opt4060_measurements.conf \
  --build_dir_suffix=fw_opt4060_measurements \
  "$@"
