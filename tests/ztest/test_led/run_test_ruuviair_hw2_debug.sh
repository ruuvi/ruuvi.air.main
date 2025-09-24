#!/bin/bash

set -x -e # Print commands and their arguments and Exit immediately if a command exits with a non-zero status.

./build_test_ruuviair_hw2_debug.sh

west flash -d build_ruuviair

