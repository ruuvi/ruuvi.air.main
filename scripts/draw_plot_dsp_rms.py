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

csv_path = test_executable_dir / 'result.csv'
df = pd.read_csv(csv_path)

# Print the dataframe to check if data is read correctly
# print(df)

# Extract x and y values
x = df['amplitude']
y_expected = df['rms_expected']
y_f32 = df['rms_f32']
y_q15 = df['rms_q15']
y_q15_cmsis = df['rms_q15_cmsis']

# Plotting
plt.figure()
plt.loglog(x, y_expected, label='RMS expected', color='black')
plt.loglog(x, y_f32, label='RMS by CMSIS-DSP arm_rms_f32', color='blue')
plt.loglog(x, y_q15_cmsis, label='RMS by CMSIS-DSP arm_rms_q15', color='red')
plt.loglog(x, y_q15, label='RMS by custom dsp_rms_q15', color='green')

# Additional plot settings
plt.xlabel("Amplitude (log scale)")
plt.ylabel("RMS (log scale)")
plt.title("RMS calculated by CMSIS-DSP and custom dsp_rms_q15")
plt.legend()
plt.grid(True)
plt.show()
