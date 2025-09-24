cmake_minimum_required(VERSION 3.20.0)

# Copyright (c) 2024, Ruuvi Innovations Ltd
# SPDX-License-Identifier: BSD-3-Clause

add_subdirectory_ifdef(CONFIG_OPT4060 opt4060)
add_subdirectory_ifdef(CONFIG_RTC_PCF85263A rtc_pcf85263a)
add_subdirectory_ifdef(CONFIG_LP5810 lp5810)

