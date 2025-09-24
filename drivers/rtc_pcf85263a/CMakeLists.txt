cmake_minimum_required(VERSION 3.20.0)

# Copyright (c) 2024, Ruuvi Innovations Ltd
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library()

zephyr_library_sources(rtc_pcf85263a.c)
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/rtc)
zephyr_library_sources_ifdef(CONFIG_PCF85263A_INT rtc_pcf85263a_int.c)
