#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev_name=RuuviAir-A1 \
  --build_mode=release \
  --prod \
  "$@"
