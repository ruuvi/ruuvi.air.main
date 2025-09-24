cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ztest_spl_calc)

target_sources(app PRIVATE
        src/test_spl_calc.c
        ../../../src/spl_calc.c
        ../../../src/spl_calc.h
        ../../../src/dsp_rms.c
        ../../../src/dsp_rms.h
        ../../../src/dsp_biquad_filter_a_weighting_16000.c
        ../../../src/dsp_biquad_filter_a_weighting_16000.h
        ../../../src/dsp_biquad_filter_a_weighting_20828.c
        ../../../src/dsp_biquad_filter_a_weighting_20828.h
        ../../../dsp/dsp_arm_biquad_cascade_df1_q15_patched.c
)

target_include_directories(app PRIVATE
        ../../../src
        ../../../dsp
)
