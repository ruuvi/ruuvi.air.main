cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ztest_moving_avg)

target_sources(app PRIVATE
        src/test_avg_accum.c
        src/test_moving_avg.c
        ../../../src/avg_accum.c
        ../../../src/avg_accum.h
        ../../../src/data_fmt_e1.c
        ../../../src/data_fmt_e1.h
        ../../../src/moving_avg.c
        ../../../src/moving_avg.h
        ../../../src/sen66_wrap.c
        ../../../src/sen66_wrap.h
        ../../../components/embedded-i2c-sen66-master/sen66_i2c.c
        ../../../components/embedded-i2c-sen66-master/sen66_i2c.h
        ../../../components/ruuvi.endpoints.c/src/ruuvi_endpoint_e1.c
        ../../../components/ruuvi.endpoints.c/src/ruuvi_endpoint_e1.h
)

target_include_directories(app PRIVATE
        ../../../src
        ../../../components/ruuvi.endpoints.c/src
        ../../../components/embedded-i2c-sen66-master
)

target_compile_definitions(app PRIVATE
        -DTEST
)

target_compile_options(app PRIVATE
        -Wno-unused-function
)
