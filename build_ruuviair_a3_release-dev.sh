#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev_name=RuuviAir-A3 \
  --build_mode=release \
  "$@"
