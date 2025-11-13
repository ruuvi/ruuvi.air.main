#!/bin/bash

./scripts/run-clang-format.sh --recursive --in-place ./drivers ./src ./fw_loader ./b0_hook ./mcuboot_hook ./tests/unity/test*/src ./tests/ztest/test*/src ./components/embedded-i2c-sen66-master

