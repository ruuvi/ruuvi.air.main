cmake_minimum_required(VERSION 3.20.0)

# Copyright (c) 2025, Ruuvi Innovations Ltd
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_LP5810 lp5810.c)
zephyr_library_include_directories(include)

