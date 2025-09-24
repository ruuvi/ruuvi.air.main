#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev=2 \
  --build_mode=debug \
  "$@"
