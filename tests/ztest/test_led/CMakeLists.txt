cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ztest_led)

target_sources(app PRIVATE src/test_led.c ../../../src/app_led.c ../../../src/app_led.h)
target_include_directories(app PRIVATE ../../../src)

