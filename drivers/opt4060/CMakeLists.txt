cmake_minimum_required(VERSION 3.20.0)

# Copyright (c) 2024, Ruuvi Innovations Ltd
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library()

zephyr_library_sources(opt4060.c)
zephyr_library_sources_ifdef(CONFIG_OPT4060_INT opt4060_int.c)
zephyr_library_include_directories(include)

