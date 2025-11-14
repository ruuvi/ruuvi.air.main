#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev_name=RuuviAir-A3 \
  --build_mode=release \
  --extra_conf=prj_fw_led_cycle_calibrate.conf \
  --build_dir_suffix=fw_led_cycle_calibrate \
  "$@"
