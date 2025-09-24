#!/usr/bin/env python3

# Copyright (c) 2024, Ruuvi Innovations Ltd
# SPDX-License-Identifier: BSD-3-Clause

# https://en.wikipedia.org/wiki/A-weighting

import scipy.signal as signal
import numpy as np
import matplotlib.pyplot as plt
import sys

# Function to format the SOS matrix as a one-dimensional C array with line feeds and indentation
def format_sos_for_c_float32(sos_matrix):
    formatted_str = ""
    for row in sos_matrix:
        formatted_row = "    " + ", ".join(f"{val:.7f}f" for val in row) + ",\n"
        formatted_str += formatted_row
    return formatted_str

def format_sos_for_c_int16(sos_matrix):
    formatted_str = ""
    for row in sos_matrix:
        formatted_row = "    " + ", ".join(f"{val}" for val in row) + ",\n"
        formatted_str += formatted_row
    return formatted_str

# Define the analog poles and zeros
poles = [-129.4, -129.4, -676.7, -4636, -76617, -76617]
zeros = [0, 0, 0, 0]  # Four zeros at the origin
k_a = 7.39705e9  # Gain factor

# Sampling frequency
fs_16khz = 16000  # 16 kHz

# Create the analog transfer function
b_s = [k_a] + [0]*4  # s^4 in the numerator
a_s = np.poly(poles)  # Poles in the denominator

# Convert to digital using the bilinear transform
b_z, a_z = signal.bilinear(b_s, a_s, fs_16khz)

# Factorize into Second-Order Sections (SOS)
sos_16khz: np.ndarray = signal.tf2sos(b_z, a_z)

# Print results
print("Second-Order Sections (SOS) for 16 kHz:")
print(sos_16khz)

# Remove the 4th column of the sos matrix since it's always 1, and it's not needed for CMSIS-DSP functions
sos_16khz_f32 = np.delete(sos_16khz, 3, axis=1)
# Change the sign of values in the 4th and 5th columns (indices 3 and 4) as it requires by CMSIS-DSP
sos_16khz_f32[:, [3, 4]] = -sos_16khz_f32[:, [3, 4]]
print("")
print("Second-Order Sections (SOS) for 16 kHz, suitable for CMSIS-DSP arm_biquad_cascade_df1_f32:")
print(f'{format_sos_for_c_float32(sos_16khz_f32)}')

scaling_factor = 2
sos_16khz_scaled = sos_16khz_f32 / scaling_factor
sos_16khz_scaled_rounded = np.round(sos_16khz_scaled * 32768)
# Check if values are within the range -1 and 1 for Q15 representation
if not np.all((sos_16khz_scaled_rounded >= -32768) & (sos_16khz_scaled_rounded < 32767)):
    print("Second-Order Sections (SOS) for 16 kHz scaled:")
    print(sos_16khz_scaled, flush=True)
    print("Second-Order Sections (SOS) for 16 kHz scaled and converted to int:")
    print(sos_16khz_scaled_rounded, flush=True)
    print("Error: Some values in sos_scaled are out of the valid range [-1, 1) for Q15 conversion.", file=sys.stderr, flush=True)
    sys.exit(1)
sos_16khz_q15 = sos_16khz_scaled_rounded.astype(np.int16)  # Convert to q15
# Add a new column with zeros at index 1 as it requires by CMSIS-DSP arm_biquad_cascade_df1_q15
sos_16khz_q15 = np.insert(sos_16khz_q15, 1, 0, axis=1)
print("Second-Order Sections (SOS) for 16 kHz scaled, suitable for CMSIS-DSP arm_biquad_cascade_df1_q15 with postShift=1:")
print(f'{format_sos_for_c_int16(sos_16khz_q15)}')


fs_20khz = 20828  # 20.828 kHz
# Create the analog transfer function
b_s = [k_a] + [0]*4  # s^4 in the numerator
a_s = np.poly(poles)  # Poles in the denominator
# Convert to digital using the bilinear transform
b_z, a_z = signal.bilinear(b_s, a_s, fs_20khz)
# Factorize into Second-Order Sections (SOS)
sos_20khz: np.ndarray = signal.tf2sos(b_z, a_z)

# Print results
print("Second-Order Sections (SOS) for 20 kHz:")
print(sos_20khz)

# Remove the 4th column of the sos matrix since it's always 1, and it's not needed for CMSIS-DSP functions
sos_20khz_f32 = np.delete(sos_20khz, 3, axis=1)
# Change the sign of values in the 4th and 5th columns (indices 3 and 4) as it requires by CMSIS-DSP
sos_20khz_f32[:, [3, 4]] = -sos_20khz_f32[:, [3, 4]]
print("")
print("Second-Order Sections (SOS) for 20 kHz, suitable for CMSIS-DSP arm_biquad_cascade_df1_f32:")
print(f'{format_sos_for_c_float32(sos_20khz_f32)}')

scaling_factor = 4
sos_20khz_scaled = sos_20khz_f32 / scaling_factor
sos_20khz_scaled_rounded = np.round(sos_20khz_scaled * 32768)
# Check if values are within the range -1 and 1 for Q15 representation
if not np.all((sos_20khz_scaled_rounded >= -32768) & (sos_20khz_scaled_rounded < 32767)):
    print("Second-Order Sections (SOS) for 20 kHz scaled:")
    print(sos_20khz_scaled, flush=True)
    print("Second-Order Sections (SOS) for 20 kHz scaled and converted to int:")
    print(sos_20khz_scaled_rounded, flush=True)
    print("Error: Some values in sos_scaled are out of the valid range [-1, 1) for Q15 conversion.", file=sys.stderr, flush=True)
    sys.exit(1)
sos_20khz_q15 = sos_20khz_scaled_rounded.astype(np.int16)  # Convert to q15
# Add a new column with zeros at index 1 as it requires by CMSIS-DSP arm_biquad_cascade_df1_q15
sos_20khz_q15 = np.insert(sos_20khz_q15, 1, 0, axis=1)
print("Second-Order Sections (SOS) for 20 kHz scaled, suitable for CMSIS-DSP arm_biquad_cascade_df1_q15 with postShift=2:")
print(f'{format_sos_for_c_int16(sos_20khz_q15)}')

# Verify frequency response
w16, h16 = signal.sosfreqz(sos_16khz, worN=2000, fs=fs_16khz)
w20, h20 = signal.sosfreqz(sos_20khz, worN=2000, fs=fs_20khz)
plt.semilogx(w16, 20 * np.log10(abs(h16)), label="16 kHz Sampling Rate")
plt.semilogx(w20, 20 * np.log10(abs(h20)), label="20.828 kHz Sampling Rate")
plt.title('Frequency Response')
plt.xlabel('Frequency [Hz]')
plt.ylabel('Amplitude [dB]')
plt.grid(which='both', linestyle='--', color='grey')
plt.legend()
plt.show()
