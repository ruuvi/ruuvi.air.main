#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
cd $SCRIPT_DIR

exec ./build.sh \
  --board=nrf52840dk \
  --board_rev_name=RuuviAir-A1 \
  --build_mode=debug \
  "$@"
