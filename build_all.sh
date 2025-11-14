#!/usr/bin/env bash

set -e -u -o pipefail

# Log directory configuration
LOG_DIR="./build_all_logs"

# Create logs directory
mkdir -p "$LOG_DIR"

declare -A results

# Build and run tests before starting builds and wait for them to complete
declare -a test_scripts=(
    "build_tests_nrf52840dk.sh"
    "build_tests_ruuvi_air.sh"
    "run_tests_native.sh"
)
echo "Starting sequential builds for tests..."
for script in "${test_scripts[@]}"; do
    if [[ -x "./$script" ]]; then
        echo "Running $script..."
        if "./$script" > "$LOG_DIR/${script}.log" 2>&1; then
            results[$script]="OK"
        else
            results[$script]="FAIL"
        fi
    else
        echo "Warning: $script not found or not executable"
        results[$script]="SKIPPED"
    fi
done

# Array to store script names and their PIDs
declare -A pids
declare -a scripts=(
    "build_nrf52840dk_a1_debug_mock.sh"
    "build_nrf52840dk_a1_debug.sh"
    "build_nrf52840dk_a1_release-dev.sh"
    "build_nrf52840dk_a1_release_mock-dev.sh"
    "build_nrf52840dk_a2_debug_mock.sh"
    "build_nrf52840dk_a2_debug.sh"
    "build_nrf52840dk_a2_release-dev.sh"
    "build_nrf52840dk_a2_release_mock-dev.sh"
    "build_ruuviair_a1_debug.sh"
    "build_ruuviair_a1_release-dev.sh"
    "build_ruuviair_a1_release-prod.sh"
    "build_ruuviair_a2_debug.sh"
    "build_ruuviair_a2_release-dev.sh"
    "build_ruuviair_a2_release-prod.sh"
    "build_ruuviair_a3_debug.sh"
    "build_ruuviair_a3_release-dev.sh"
    "build_ruuviair_a3_release-prod.sh"
    "build_ruuviair_a3_debug_fw_led_cycle_calibrate.sh"
    "build_ruuviair_a3_debug_fw_led_cycle_rgbw.sh"
    "build_ruuviair_a3_debug_fw_opt4060_measurements.sh"
    "build_ruuviair_a3_release_fw_led_cycle_calibrate.sh"
    "build_ruuviair_a3_release_fw_led_cycle_rgbw.sh"
    "build_ruuviair_a3_release_fw_opt4060_measurements.sh"
)

# Start all builds in parallel with minimal priority
echo "Starting parallel builds..."
for script in "${scripts[@]}"; do
    if [[ -x "./$script" ]]; then
        nice -n 19 ionice -c 3 "./$script" > "$LOG_DIR/${script}.log" 2>&1 &
        pids[$script]=$!
        echo "Started $script (PID: ${pids[$script]})"
    else
        echo "Warning: $script not found or not executable"
    fi
done

# Wait for all processes to complete
echo "Waiting for all builds to complete..."
for script in "${scripts[@]}"; do
    if [[ -n "${pids[$script]:-}" ]]; then
        if wait ${pids[$script]}; then
            results[$script]="OK"
        else
            results[$script]="FAIL"
        fi
    else
        results[$script]="SKIPPED"
    fi
done

# Disable exit on error for results processing
set +e

# Print results table with colors
echo
echo "========================================"
echo "           BUILD RESULTS"
echo "========================================"

SCRIPT_WIDTH=60
printf "%-${SCRIPT_WIDTH}s %s\n" "Script" "Status"
echo "----------------------------------------"

# Combine all scripts for results display and count results
all_scripts=("${test_scripts[@]}" "${scripts[@]}")
failed_count=0
total_count=0

for script in "${all_scripts[@]}"; do
    if [[ "${results[$script]:-}" == "OK" ]]; then
        printf "%-${SCRIPT_WIDTH}s \033[32m%s\033[0m\n" "$script" "OK"
        total_count=$((total_count + 1))
    elif [[ "${results[$script]:-}" == "FAIL" ]]; then
        printf "%-${SCRIPT_WIDTH}s \033[31m%s\033[0m\n" "$script" "FAIL"
        failed_count=$((failed_count + 1))
        total_count=$((total_count + 1))
    else
        printf "%-${SCRIPT_WIDTH}s \033[33m%s\033[0m\n" "$script" "SKIPPED"
    fi
done

echo "========================================"
echo "Logs available in $LOG_DIR directory"

# Print final status and set exit code
echo
if [[ $failed_count -eq 0 ]]; then
    echo -e "\033[32mFINAL STATUS: ALL BUILDS SUCCESSFUL ($total_count/$total_count)\033[0m"
    exit 0
else
    echo -e "\033[31mFINAL STATUS: $failed_count/$total_count BUILDS FAILED\033[0m"
    exit 1
fi

