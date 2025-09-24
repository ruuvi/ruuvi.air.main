#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev=2 \
  --build_mode=debug \
  --extra_conf=prj_fw_led_cycle_calibrate.conf \
  --build_dir_suffix=fw_led_cycle_calibrate \
  "$@"
