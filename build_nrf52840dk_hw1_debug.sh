#!/bin/bash

exec ./build.sh \
  --board=nrf52840dk \
  --board_rev=1 \
  --build_mode=debug \
  "$@"
