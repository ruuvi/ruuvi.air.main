#!/bin/bash

exec ./build.sh \
  --board=ruuviair \
  --board_rev=1 \
  --build_mode=release \
  "$@"
