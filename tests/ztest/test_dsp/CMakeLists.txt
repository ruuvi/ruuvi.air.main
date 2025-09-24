cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ztest_dsp)

target_sources(app PRIVATE
        src/test_dsp_rms.c
        src/test_dsp_biquad_filter_a_weighting_16000.c
        ../../../src/dsp_rms.c
        ../../../src/dsp_rms.h
        ../../../src/dsp_biquad_filter_a_weighting_16000.c
        ../../../src/dsp_biquad_filter_a_weighting_16000.h
        ../../../dsp/dsp_arm_biquad_cascade_df1_q15_patched.c
)

target_include_directories(app PRIVATE
        ../../../src
        ../../../dsp
)
