#!/usr/bin/env bash

set -euo pipefail

flag_check=false

for arg in "$@"; do
  case $arg in
    --check)
      flag_check=true
      shift
      ;;
    *)
      echo "Error: Unknown argument '$1'" >&2
      exit 1
      ;;
  esac
done


CLANG_PATHS=(
    ./include
    ./drivers
    ./src
    ./fw_loader
    ./b0_hook
    ./mcuboot_hook
    ./tests/unity/test*/src
    ./tests/ztest/test*/src
    ./components/embedded-i2c-sen66-master
)

DTS_PATHS=(
    .
    ./fw_loader
    ./boards/ruuvi/ruuviair
    ./boards/nordic/nrf52840dk_ruuviair
    ./sysbuild/b0
    ./sysbuild/mcuboot
)

RUN_CLANG=./scripts/run-clang-format.sh
RUN_DT=./scripts/run-dt-format.sh

if [ "$flag_check" = false ]; then
  CLANG_EXTRA_ARGS="--in-place"
else
  CLANG_EXTRA_ARGS=""
fi

if [ "$flag_check" = false ]; then
  DT_EXTRA_ARGS="--inplace"
else
  DT_EXTRA_ARGS=""
fi

# Format C/C++
"$RUN_CLANG" --recursive $CLANG_EXTRA_ARGS "${CLANG_PATHS[@]}"

# Format DeviceTree
"$RUN_DT" --tabs $DT_EXTRA_ARGS "${DTS_PATHS[@]}"
