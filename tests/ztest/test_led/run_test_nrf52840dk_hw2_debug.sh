#!/bin/bash

set -x -e # Print commands and their arguments and Exit immediately if a command exits with a non-zero status.

./build_test_nrf52840dk_hw2_debug.sh

west flash -d build_nrf52840dk

