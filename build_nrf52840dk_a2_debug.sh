#!/bin/bash

exec ./build.sh \
  --board=nrf52840dk \
  --board_rev_name=RuuviAir-A2 \
  --build_mode=debug \
  "$@"
