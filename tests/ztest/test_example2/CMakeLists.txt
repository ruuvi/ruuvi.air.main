cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ztest_example2)

FILE(GLOB app_sources src/*.c src/uut/*.c src/foo/*.c)
target_sources(app PRIVATE ${app_sources})
target_include_directories(app PRIVATE src/uut src)

target_link_options(app INTERFACE "-Wl,--wrap=foo_init")

