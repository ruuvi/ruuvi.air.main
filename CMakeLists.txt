# @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.

cmake_minimum_required(VERSION 3.20.0)

# Make CMake search our modules first (before Zephyr's)
SET(MOD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
list(PREPEND CMAKE_MODULE_PATH "${MOD_DIR}")

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/drivers)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ruuvi.air.c)

set_property(TARGET app_version_h PROPERTY APP_VERSION_CUSTOMIZATION
    "//#include;\\\"app_version_extra.h\\\";"
)

include("${MOD_DIR}/git_describe_tag.cmake")

SET(TAG_PREFIX "v")
SET(TAG_WORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

set(GENERATED_VERSION_BIN "${CMAKE_CURRENT_BINARY_DIR}/VERSION")
set(GENERATED_VERSION "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
# Generate VERSION on every build (ALWAYS re-run keeps it current w/ HEAD and dirty state)
add_custom_target(gen_version_always ALL
  COMMAND ${CMAKE_COMMAND} -DOUT=${GENERATED_VERSION_BIN}
                           -DWORKDIR=${TAG_WORK_DIR}
                           -DTAG_PREFIX=${TAG_PREFIX}
                           -P ${MOD_DIR}/gen_version.cmake
  # Sync to source tree only if content changed
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GENERATED_VERSION_BIN}" "${GENERATED_VERSION}"
  COMMENT "Generating VERSION (always) and syncing if changed"
  VERBATIM
)

set(VH ${PROJECT_BINARY_DIR}/zephyr/include/generated/zephyr/app_version.h)
set(MYVH ${PROJECT_BINARY_DIR}/zephyr/include/generated/zephyr/app_version.override.h)

git_describe_tag(${TAG_WORK_DIR} ${TAG_PREFIX} APP_BUILD_VERSION)

add_custom_command(
  OUTPUT ${MYVH}
  COMMAND ${CMAKE_COMMAND} -DZEPHYR_BASE=${ZEPHYR_BASE}
    -DOUT_FILE=${MYVH}
    -DVERSION_TYPE=APP
    -DVERSION_FILE=${GENERATED_VERSION}
    -DAPP_VERSION_CUSTOMIZATION="$<TARGET_PROPERTY:app_version_h,APP_VERSION_CUSTOMIZATION>"
    -DAPP_BUILD_VERSION=${APP_BUILD_VERSION}
    -DCMAKE_MODULE_PATH=${MOD_DIR}
    -P ${ZEPHYR_BASE}/cmake/gen_version_h.cmake
  DEPENDS gen_version_always ${GENERATED_VERSION} ${git_dependency}
  COMMAND_EXPAND_LISTS
)

# stamp so we don't collide with Zephyr's rule that already outputs ${VH}
add_custom_command(
  OUTPUT ${MYVH}.stamp
  COMMAND ${CMAKE_COMMAND} -E copy ${MYVH} ${VH}
  COMMAND ${CMAKE_COMMAND} -E touch ${MYVH}.stamp
  DEPENDS version_h ${MYVH}             # ensure the default header exists first
)

add_custom_target(override_version_h ALL
  DEPENDS ${MYVH}.stamp)

# Make sure all generated headers wait for our override too
add_dependencies(zephyr_generated_headers gen_version_always override_version_h)
# Make the Zephyr 'app' target depend on the VERSION file and generted header
add_dependencies(app gen_version_always override_version_h)


add_subdirectory(components/ruuvi.endpoints.c)

target_sources(app PRIVATE
        src/aqi.c
        src/aqi.h
        src/api.c
        src/api.h
        src/app_button.c
        src/app_button.h
        src/app_button_cb.c
        src/app_button_cb.h
        src/app_early_init.c
        src/app_ext_flash_and_sensors_power.c
        src/app_ext_flash_and_sensors_power.h
        src/app_gpio_input.c
        src/app_gpio_input.h
        src/app_led.c
        src/app_led.h
        src/app_rtc.c
        src/app_rtc.h
        src/app_segger_rtt.c
        src/app_segger_rtt.h
        src/app_settings.c
        src/app_settings.h
        src/app_supercap.c
        src/app_supercap.h
        src/avg_accum.c
        src/avg_accum.h
        src/ble_adv.c
        src/ble_adv.h
        src/ble_mgmt_hooks.c
        src/ble_mgmt_hooks.h
        src/data_fmt_e0.c
        src/data_fmt_e0.h
        src/data_fmt_f0.c
        src/data_fmt_f0.h
        src/data_fmt_e1.c
        src/data_fmt_e1.h
        src/data_fmt_6.c
        src/data_fmt_6.h
        src/dsp_biquad_filter_a_weighting_16000.c
        src/dsp_biquad_filter_a_weighting_16000.h
        src/dsp_biquad_filter_a_weighting_20828.c
        src/dsp_biquad_filter_a_weighting_20828.h
        src/dsp_rms.c
        src/dsp_rms.h
        src/hist_log.c
        src/hist_log.h
        src/main.c
        src/nfc.c
        src/nfc.h
        src/nus.c
        src/nus.h
        src/nus_req.c
        src/nus_req.h
        src/led_calibration.c
        src/led_calibration.h
        src/lp5810_test.c
        src/lp5810_test.h
        src/mic_pdm.c
        src/mic_pdm.h
        src/mic_spg08p4hm4h.h
        src/moving_avg.c
        src/moving_avg.h
        src/opt_rgb_ctrl.c
        src/opt_rgb_ctrl.h
        src/rgb_led.c
        src/rgb_led.h
        src/rgb_led_types.h
        src/sensirion_i2c_hal.c
        src/sensors.c
        src/sensors.h
        src/sen66_wrap.c
        src/sen66_wrap.h
        src/spl_calc.c
        src/spl_calc.h
        src/utils.c
        src/utils.h
        dsp/dsp_arm_biquad_cascade_df1_q15_patched.c
        dsp/dsp_arm_biquad_cascade_df1_q15_patched.h
        components/embedded-i2c-sen66-master/sen66_i2c.c
        components/embedded-i2c-sen66-master/sen66_i2c.h
        components/embedded-i2c-sen66-master/sensirion_common.c
        components/embedded-i2c-sen66-master/sensirion_common.h
        components/embedded-i2c-sen66-master/sensirion_config.h
        components/embedded-i2c-sen66-master/sensirion_i2c.c
        components/embedded-i2c-sen66-master/sensirion_i2c.h
)

target_include_directories(app PRIVATE
        components/embedded-i2c-sen66-master
        dsp
        drivers/lp5810/include
        drivers/opt4060/include
        ${ZEPHYR_BASE}/kernel/include
        ${ARCH_DIR}/${ARCH}/include
)

target_link_options(app INTERFACE
        "-Wl,--wrap=sys_reboot"
)

# set_compiler_property(PROPERTY debug -g0)
