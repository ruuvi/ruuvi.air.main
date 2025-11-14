#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
cd $SCRIPT_DIR

exec ./build.sh \
  --board=ruuviair \
  --board_rev_name=RuuviAir-A3 \
  --build_mode=debug \
  "$@"
