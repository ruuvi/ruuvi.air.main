#!/bin/bash

exec ./build.sh \
  --board=nrf52840dk \
  --board_rev_name=RuuviAir-A1 \
  --build_mode=debug \
  "$@"
