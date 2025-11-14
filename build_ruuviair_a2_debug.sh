#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev_name=RuuviAir-A2 \
  --build_mode=debug \
  "$@"
