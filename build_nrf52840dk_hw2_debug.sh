#!/bin/bash

exec ./build.sh \
  --board=nrf52840dk \
  --board_rev=2 \
  --build_mode=debug \
  "$@"
