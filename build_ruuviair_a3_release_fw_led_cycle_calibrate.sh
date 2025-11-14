#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
cd $SCRIPT_DIR

exec ./build.sh \
  --board=ruuviair \
  --board_rev_name=RuuviAir-A3 \
  --build_mode=release \
  --extra_conf=prj_fw_led_cycle_calibrate.conf \
  --build_dir_suffix=fw_led_cycle_calibrate \
  "$@"
