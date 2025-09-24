#!/usr/bin/env python3

# Copyright (c) 2024, Ruuvi Innovations Ltd
# SPDX-License-Identifier: BSD-3-Clause

import matplotlib.pyplot as plt
import pandas as pd
import sys
import os
import subprocess
from pathlib import Path

script_full_path = Path(__file__).resolve()
script_name = script_full_path.stem
script_dir = script_full_path.parent
prj_path = script_dir / '..'
test_name = f'test_{script_name}'
test_dir = Path(prj_path) / 'tests/ztest' / test_name
build_dir = test_dir / 'build'
original_dir = os.getcwd()

if not os.path.exists(test_dir):
    print(f"The test directory {test_dir} does not exist.")
    sys.exit(1)

try:
    os.chdir(test_dir)
    subprocess.run(['nrfutil', 'toolchain-manager', 'launch', 'west', '--', 'build', '-b', 'native_sim'], check=True)
except subprocess.CalledProcessError as e:
    print(f"An error occurred during 'west build': {e}", file=sys.stderr)
    sys.exit(1)
finally:
    os.chdir(original_dir)

print(f'Build successful.')
print(f'')
print(f'Running the test...')

test_executable_dir = build_dir / test_name / 'zephyr'
if not os.path.exists(test_executable_dir):
    test_executable_dir = build_dir / 'zephyr'
test_executable_path = test_executable_dir / 'zephyr.elf'
try:
    os.chdir(test_executable_dir)
    subprocess.run([test_executable_path], check=True)
except subprocess.CalledProcessError as e:
    print(f"An error occurred during 'west test': {e}", file=sys.stderr)
    sys.exit(1)
finally:
    os.chdir(original_dir)

csv_path_16000 = test_executable_dir / 'result_16000.csv'
df = pd.read_csv(csv_path_16000)

# Print the dataframe to check if data is read correctly
# print(df)

# Extract x and y values
x = df['freq']
y_rms_unfiltered = df['rms_unfiltered']
y_rms_f32_filtered = df['rms_f32_filtered']
y_rms_q15_filtered_cmsis = df['rms_q15_filtered_cmsis']
y_rms_q15_filtered_patched = df['rms_q15_filtered_patched']

# Plotting for sampling rate 16 kHz
plt.figure()
plt.semilogx(x, y_rms_unfiltered, label='RMS unfiltered', color='blue')
plt.semilogx(x, y_rms_f32_filtered, label='RMS by biquad_cascade_df1_f32', color='black')
plt.semilogx(x, y_rms_q15_filtered_cmsis, label='RMS by biquad_cascade_df1_q15 (CMSIS)', color='red')
plt.semilogx(x, y_rms_q15_filtered_patched, label='RMS by biquad_cascade_df1_q15 (patched)', color='green')

# Additional plot settings
plt.xlabel("Frequency (log scale)")
plt.ylabel("RMS")
plt.title("RMS after applying A-weighting filter (sampling rate 16 kHz)")
plt.legend()
plt.grid(True)
plt.show()

# Plotting for sampling rate 20 kHz

csv_path_20828 = test_executable_dir / 'result_20828.csv'
df = pd.read_csv(csv_path_20828)

# Print the dataframe to check if data is read correctly
# print(df)

# Extract x and y values
x = df['freq']
y_rms_unfiltered = df['rms_unfiltered']
y_rms_f32_filtered = df['rms_f32_filtered']
y_rms_q15_filtered_cmsis = df['rms_q15_filtered_cmsis']
y_rms_q15_filtered_patched = df['rms_q15_filtered_patched']

plt.figure()
plt.semilogx(x, y_rms_unfiltered, label='RMS unfiltered', color='blue')
plt.semilogx(x, y_rms_f32_filtered, label='RMS by biquad_cascade_df1_f32', color='black')
plt.semilogx(x, y_rms_q15_filtered_cmsis, label='RMS by biquad_cascade_df1_q15 (CMSIS)', color='red')
plt.semilogx(x, y_rms_q15_filtered_patched, label='RMS by biquad_cascade_df1_q15 (patched)', color='green')

# Additional plot settings
plt.xlabel("Frequency (log scale)")
plt.ylabel("RMS")
plt.title("RMS after applying A-weighting filter (sampling rate 20.828 kHz)")
plt.legend()
plt.grid(True)
plt.show()
