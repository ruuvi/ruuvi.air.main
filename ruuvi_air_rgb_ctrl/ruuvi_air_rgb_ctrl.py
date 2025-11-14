import tkinter as tk
from tkinter import ttk
import threading
from datetime import datetime
import time
import asyncio
import os
import sys
import re
import io
import csv
import numpy as np
from concurrent.futures import Future
from enum import Enum

requirements = ['bleak', 'smp', 'smpclient', 'numpy']

LABEL_WIDTH = 20

BRIGHTNESS_MIN = 5
BRIGHTNESS_RANGE = 100 - BRIGHTNESS_MIN
AQI_BRIGHTNESS_MIN_PERCENT = 25.0
AQI_BRIGHTNESS_MAX_PERCENT = 100.0
AQI_BRIGHTNESS_RANGE_PERCENT = AQI_BRIGHTNESS_MAX_PERCENT - AQI_BRIGHTNESS_MIN_PERCENT

BRIGHTNESS_NIGHT = 5
BRIGHTNESS_DAY = 15
BRIGHTNESS_BRIGHT_DAY = 64

CURRENTS = {
    'Night': [12, 2, 10],
    'Day': [35, 6, 20],
    'BrightDay': [150, 70, 255]
}

AQI_COLORS = {
    'Night':
        {
            'Excellent': {'R': 0, 'G': 255, 'B': 90},
            'Good': {'R': 30, 'G': 255, 'B': 0},
            'Fair': {'R': 240, 'G': 255, 'B': 0},
            'Poor': {'R': 255, 'G': 80, 'B': 0},
            'VeryPoor': {'R': 255, 'G': 0, 'B': 0},
        },
    'Day':
        {
            'Excellent': {'R': 0, 'G': 255, 'B': 90},
            'Good': {'R': 30, 'G': 255, 'B': 0},
            'Fair': {'R': 240, 'G': 255, 'B': 0},
            'Poor': {'R': 255, 'G': 80, 'B': 0},
            'VeryPoor': {'R': 255, 'G': 0, 'B': 0},
        },
    'BrightDay':
        {
            'Excellent': {'R': 0, 'G': 255, 'B': 90},
            'Good': {'R': 30, 'G': 255, 'B': 0},
            'Fair': {'R': 255, 'G': 160, 'B': 0},
            'Poor': {'R': 255, 'G': 80, 'B': 0},
            'VeryPoor': {'R': 255, 'G': 0, 'B': 0},
        },
    'Percent':
        {
            'Excellent': {'R': 0, 'G': 255, 'B': 90},
            'Good': {'R': 30, 'G': 255, 'B': 0},
            'Fair': {'R': 240, 'G': 255, 'B': 0},
            'Poor': {'R': 255, 'G': 80, 'B': 0},
            'VeryPoor': {'R': 255, 'G': 0, 'B': 0},
        },
}

def check_environment():
    def realpath(path):
        return os.path.normcase(os.path.realpath(path))

    if sys.version_info[0] < 3:
        print("WARNING: Support for Python 2 is deprecated and will be removed in future versions.")
    elif sys.version_info[0] == 3 and sys.version_info[1] < 10:
        print("WARNING: Python 3 versions older than 3.10 are not supported.")

    from importlib.metadata import version as package_version, PackageNotFoundError
    list_of_requirements = requirements
    list_of_requirements_from_file = []
    requirements_path = realpath(os.path.join(os.path.dirname(__file__), "requirements.txt"))
    if os.path.isfile(requirements_path):
        with open(requirements_path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                if line.startswith('file://'):
                    line = os.path.basename(line)
                if line.startswith('-e') and '#egg=' in line:
                    line = re.search(r'#egg=(\S+)', line).group(1)
                list_of_requirements_from_file.append(line)
        diff1 = [item for item in requirements if item not in list_of_requirements_from_file]
        if diff1:
            print(f"WARNING: The following Python requirements are missing in the {requirements_path}: {diff1}")
            sys.exit(1)
        list_of_requirements = list_of_requirements_from_file

    not_satisfied = []
    for package in list_of_requirements:
        try:
            package_version(package)
        except PackageNotFoundError:
            not_satisfied.append(package)

    if not_satisfied:
        print(f'The following Python requirements are not satisfied: {not_satisfied}')
        if list_of_requirements_from_file:
            print(f'Install them using the command: pip install -r {requirements_path}')
        else:
            print(f'Install them using the command: pip install {" ".join(not_satisfied)}')
        sys.exit(1)

check_environment()

from bleak import BleakScanner
from bleak.exc import BleakDBusError
from smpclient import SMPClient
from smpclient.transport.ble import SMPBLETransport
from smpclient.transport import SMPTransport
from smpclient.requests.shell_management import Execute

# --- Constants ---
SERVICE_UUID = "0000fc98-0000-1000-8000-00805f9b34fb"
SCAN_DURATION = 7  # seconds
MAX_LOG_LINES = 1000
DEBOUNCE_DELAY_S = 0.05  # 50ms delay to coalesce rapid slider changes

# CSV data for brightness to LED current conversion
BRIGHTNESS_TO_CURRENT_CSV = '''Percent,d_R,d_G,d_B,dim_R,dim_G,dim_B,c_R,c_G,c_B
0,0,0,0,0,0,0,0.0,0.0,0.0
1,4,1,2,191,134,138,3.2238,0.525,1.2306
2,6,2,3,235,131,171,5.5981,1.0476,2.1331
3,8,2,4,249,196,183,7.8425,1.5473,3.0161
4,11,3,4,232,174,244,10.0742,2.0483,3.8537
5,13,3,5,239,218,238,12.2949,2.5623,4.6992
6,15,4,6,246,197,234,14.4997,3.0773,5.5498
7,17,4,7,250,230,233,16.6728,3.5986,6.4171
8,19,5,8,252,213,231,18.8086,4.1257,7.2938
9,22,5,9,243,239,230,21.0907,4.6726,8.1563
10,24,6,10,246,225,230,23.215,5.2317,9.0197
11,26,6,10,249,248,253,25.371,5.8093,9.9254
12,28,7,11,251,234,250,27.5774,6.3866,10.806
13,30,7,12,252,254,250,29.6662,6.9639,11.7334
14,32,8,13,255,241,248,31.9626,7.5287,12.6574
15,35,9,14,249,233,247,34.05,8.1044,13.5783
16,37,9,15,250,249,248,36.3585,8.7399,14.5504
17,39,10,16,252,242,249,38.5705,9.3839,15.56
18,41,11,17,254,235,249,40.7631,10.0332,16.5473
19,43,11,18,255,248,248,42.9772,10.6747,17.5064
20,46,12,19,250,242,248,45.0531,11.3159,18.4721
21,48,12,20,252,254,248,47.3227,11.9571,19.4577
22,50,13,21,252,249,249,49.5224,12.6379,20.456
23,52,14,22,254,246,250,51.7529,13.3476,21.4899
24,55,15,23,250,241,250,54.0535,14.0789,22.5176
25,57,15,24,251,251,251,56.1481,14.75,23.5767
26,59,16,25,253,248,251,58.3949,15.4663,24.6285
27,61,17,26,253,245,252,60.6417,16.1993,25.6606
28,63,17,27,254,254,252,62.82,16.9062,26.7082
29,66,18,28,252,251,253,65.0469,17.6252,27.7317
30,68,19,29,253,249,254,67.2434,18.3801,28.9191
31,70,20,31,253,246,247,69.5531,19.1606,30.0586
32,72,20,32,254,254,250,71.842,19.9002,31.0682
33,74,21,33,255,251,251,73.9774,20.6365,32.3528
34,77,22,34,253,250,251,76.3325,21.414,33.4503
35,79,23,35,253,249,252,78.5564,22.2435,34.5217
36,81,24,36,255,247,252,80.8591,23.0956,35.6168
37,84,24,37,253,254,254,83.2316,23.9054,36.8366
38,86,25,38,254,252,255,85.4913,24.6367,37.9687
39,88,26,40,255,251,250,87.9202,25.4277,39.1394
40,91,27,41,253,249,252,90.231,26.268,40.3483
41,93,28,42,254,248,252,92.5459,27.0665,41.5327
42,95,28,43,255,254,254,94.8992,27.8897,42.7421
43,98,29,44,254,253,255,97.521,28.7428,43.9874
44,100,30,46,255,252,251,99.99,29.59,45.0997
45,103,31,47,253,252,252,102.1121,30.5315,46.3832
46,105,32,48,254,251,254,104.7151,31.4506,47.7677
47,108,33,49,253,251,254,107.1619,32.2957,48.898
48,110,34,51,253,251,251,109.4439,33.1942,50.1804
49,112,35,52,255,250,253,111.8823,34.1236,51.5069
50,115,36,53,253,249,254,114.2421,35.0179,52.7443
51,117,36,54,254,254,255,116.5151,35.8912,53.9236
52,120,37,56,254,255,252,119.2639,36.8995,55.2317
53,122,38,57,254,254,254,121.7712,37.7975,56.6603
54,125,39,58,254,254,254,124.3842,38.7251,57.8665
55,127,40,60,254,253,252,126.5524,39.6534,59.2241
56,129,41,61,255,253,253,128.9405,40.5955,60.4584
57,132,42,62,254,253,255,131.3782,41.5961,61.8729
58,134,43,64,255,253,253,133.9745,42.5386,63.2529
59,137,44,65,254,253,253,136.5976,43.5208,64.5447
60,140,45,66,254,253,254,139.3368,44.5077,65.7862
61,142,46,68,254,253,253,141.615,45.4656,67.285
62,145,47,69,254,253,254,144.2087,46.5432,68.6546
63,147,48,70,254,253,255,146.6246,47.5574,69.9814
64,150,49,72,254,254,253,149.1847,48.5839,71.2735
65,153,50,73,253,254,255,152.0446,49.6676,72.9078
66,155,51,75,254,254,253,154.697,50.7069,74.1248
67,158,52,76,254,254,254,157.252,51.735,75.586
68,161,53,78,254,254,252,160.1468,52.6852,77.1569
69,163,54,79,254,254,254,162.628,53.7288,78.5057
70,166,56,80,254,252,255,165.3528,55.0592,79.9939
71,169,57,82,253,251,254,168.1223,56.0747,81.4251
72,171,57,83,255,255,255,170.8423,56.953,82.9416
73,174,58,85,254,255,254,173.3694,57.9863,84.4907
74,177,60,87,254,253,252,176.1264,59.1051,86.0842
75,180,61,88,254,253,254,179.0168,60.3775,87.4671
76,182,62,89,254,253,255,181.498,61.4479,88.9896
77,185,63,91,255,254,254,184.2023,62.5631,90.498
78,188,64,93,254,255,253,187.2855,63.9168,92.1526
79,190,65,94,255,255,254,189.9081,64.9049,93.6477
80,194,67,96,253,253,253,193.0722,66.0595,95.1729
81,196,68,98,255,253,253,195.7956,67.2978,97.0141
82,199,69,99,254,254,254,198.4008,68.4191,98.4803
83,202,70,101,254,254,253,201.1959,69.5874,100.1652
84,205,71,102,254,255,255,204.2234,70.8077,101.7796
85,208,73,104,255,253,254,207.5771,72.0882,103.357
86,211,74,106,254,253,253,210.5917,73.2369,105.0713
87,214,75,107,255,254,254,213.5259,74.4287,106.5467
88,217,76,109,255,255,254,216.8853,75.7771,108.283
89,221,77,110,254,254,255,220.1268,76.7926,109.8773
90,223,79,112,255,253,254,222.9041,78.1569,111.7049
91,227,80,114,254,253,254,226.012,79.298,113.3779
92,230,81,116,254,254,253,229.1899,80.5157,115.0356
93,233,82,117,255,255,255,232.3791,81.9174,116.7713
94,236,84,119,254,253,254,235.4083,83.2327,118.3728
95,239,85,121,254,254,254,238.6175,84.383,120.2201
96,243,86,122,254,254,255,242.0759,85.7388,121.905
97,245,88,124,255,253,254,244.8009,87.0915,123.5518
98,249,89,126,255,254,254,248.5315,88.3537,125.2481
99,252,90,128,255,254,253,251.6679,89.654,127.1762
100,255,91,129,255,255,255,255.0,90.9823,128.7457
'''

# Precomputed arrays for brightness to current conversion
R_CURRENT_ARRAY = None
G_CURRENT_ARRAY = None
B_CURRENT_ARRAY = None
R_PWM_ARRAY = None
G_PWM_ARRAY = None
B_PWM_ARRAY = None

# Function to initialize the brightness arrays
def initialize_brightness_arrays():
    global R_CURRENT_ARRAY, G_CURRENT_ARRAY, B_CURRENT_ARRAY
    global R_PWM_ARRAY, G_PWM_ARRAY, B_PWM_ARRAY

    # Read CSV data into memory
    csv_data = io.StringIO(BRIGHTNESS_TO_CURRENT_CSV)
    reader = csv.DictReader(csv_data)
    data = list(reader)
    assert len(data) == 101, "Expected 101 rows in CSV, got {}".format(len(data))

    # Convert data to numpy arrays for interpolation
    percents = np.array([np.uint8(row['Percent']) for row in data])
    R_CURRENT_ARRAY = np.array([np.uint8(row['d_R']) for row in data])
    G_CURRENT_ARRAY = np.array([np.uint8(row['d_G']) for row in data])
    B_CURRENT_ARRAY = np.array([np.uint8(row['d_B']) for row in data])
    R_PWM_ARRAY = np.array([np.uint8(row['dim_R']) for row in data])
    G_PWM_ARRAY = np.array([np.uint8(row['dim_G']) for row in data])
    B_PWM_ARRAY = np.array([np.uint8(row['dim_B']) for row in data])


# Call initialization function
initialize_brightness_arrays()

# Function to get LED currents for a given brightness percentage (0-100)
def get_led_currents_for_brightness(brightness_percent):
    # Ensure brightness is within range
    brightness_percent = int(max(0, min(100, brightness_percent)))

    # Return precomputed integer values from arrays
    return (int(R_CURRENT_ARRAY[brightness_percent]), 
            int(G_CURRENT_ARRAY[brightness_percent]), 
            int(B_CURRENT_ARRAY[brightness_percent]))


def get_pwm_compensation_for_brightness(brightness_percent, pwm_r, pwm_g, pwm_b):
    return (int(round(float(R_PWM_ARRAY[int(brightness_percent)]) * pwm_r / 255)),
        int(round(float(G_PWM_ARRAY[int(brightness_percent)]) * pwm_g / 255)),
        int(round(float(B_PWM_ARRAY[int(brightness_percent)]) * pwm_b / 255)))


def rgb_to_hsl(r, g, b):
    """Convert RGB values (0-255) to HSL format."""
    r, g, b = r/255.0, g/255.0, b/255.0
    max_val = max(r, g, b)
    min_val = min(r, g, b)
    h, s, l = 0, 0, (max_val + min_val) / 2

    if max_val == min_val:
        h = s = 0  # achromatic
    else:
        d = max_val - min_val
        s = d / (2 - max_val - min_val) if l > 0.5 else d / (max_val + min_val)
        if max_val == r:
            h = (g - b) / d + (6 if g < b else 0)
        elif max_val == g:
            h = (b - r) / d + 2
        elif max_val == b:
            h = (r - g) / d + 4
        h /= 6

    return h * 360, s * 100, l * 100


def rgb_to_hsv(r, g, b):
    """Convert RGB values (0-255) to HSV format."""
    r, g, b = r/255.0, g/255.0, b/255.0
    max_val = max(r, g, b)
    min_val = min(r, g, b)
    h, s, v = 0, 0, max_val

    d = max_val - min_val
    s = 0 if max_val == 0 else d / max_val

    if max_val == min_val:
        h = 0  # achromatic
    else:
        if max_val == r:
            h = (g - b) / d + (6 if g < b else 0)
        elif max_val == g:
            h = (b - r) / d + 2
        elif max_val == b:
            h = (r - g) / d + 4
        h /= 6

    return h * 360, s * 100, v * 100


def hsl_to_rgb(h, s, l):
    """Convert HSL values (H: 0-360, S: 0-100, L: 0-100) to RGB format (0-255)."""
    h = h / 360.0
    s = s / 100.0
    l = l / 100.0

    def hue_to_rgb(p, q, t):
        if t < 0:
            t += 1
        if t > 1:
            t -= 1
        if t < 1/6:
            return p + (q - p) * 6 * t
        if t < 1/2:
            return q
        if t < 2/3:
            return p + (q - p) * (2/3 - t) * 6
        return p

    if s == 0:
        r = g = b = l  # achromatic
    else:
        q = l * (1 + s) if l < 0.5 else l + s - l * s
        p = 2 * l - q
        r = hue_to_rgb(p, q, h + 1/3)
        g = hue_to_rgb(p, q, h)
        b = hue_to_rgb(p, q, h - 1/3)

    return int(round(r * 255)), int(round(g * 255)), int(round(b * 255))


def hsv_to_rgb(h, s, v):
    """Convert HSV values (H: 0-360, S: 0-100, V: 0-100) to RGB format (0-255)."""
    h = h / 360.0
    s = s / 100.0
    v = v / 100.0

    i = int(h * 6.0)
    f = (h * 6.0) - i
    p = v * (1.0 - s)
    q = v * (1.0 - s * f)
    t = v * (1.0 - s * (1.0 - f))

    i = i % 6
    if i == 0:
        r, g, b = v, t, p
    elif i == 1:
        r, g, b = q, v, p
    elif i == 2:
        r, g, b = p, v, t
    elif i == 3:
        r, g, b = p, q, v
    elif i == 4:
        r, g, b = t, p, v
    elif i == 5:
        r, g, b = v, p, q

    return int(round(r * 255)), int(round(g * 255)), int(round(b * 255))


class LogLevel(Enum):
    """Defines the logging levels for the application."""
    DBG = 0  # Detailed debug information
    INF = 1  # Standard operational logs
    WRN = 2  # Warnings for non-critical issues
    ERR = 3  # Errors and exceptions


class StatusColor(Enum):
    """Defines the colors for the status indicator."""
    GREY = "grey"
    GREEN = "green"
    RED = "red"


class BLELEDControllerApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("BLE LED Controller")
        self.geometry("1280x1024")

        # Make sure brightness arrays are initialized
        if R_CURRENT_ARRAY is None or G_CURRENT_ARRAY is None or B_CURRENT_ARRAY is None:
            initialize_brightness_arrays()

        # --- State Variables ---
        self.client: SMPClient | None = None
        self.scanning = False
        self.discovered_devices = {}
        self.slider_keys = ['R_current', 'G_current', 'B_current', 'R_PWM', 'G_PWM', 'B_PWM']

        # RGB values
        self.rgb_values = {'R': 0, 'G': 0, 'B': 0}
        self.rgb_dimmed = {'R': 0, 'G': 0, 'B': 0}

        # AQI value with precision 0.1
        self.aqi_value = 0.0

        # Common brightness value (0-100)
        self.brightness = 20

        # PWM brightness value (0-100)
        self.hsv_brightness = 100

        self.aqi_brightness_label = None
        self.aqi_value_label = None

        # --- Logging Configuration ---
        self.log_level = LogLevel.INF

        # --- Asyncio and Concurrency Control ---
        self.async_loop = asyncio.new_event_loop()
        self.async_thread = threading.Thread(target=self.async_loop.run_forever, daemon=True)
        self.async_thread.start()

        self.command_lock = asyncio.Lock()
        self.send_task: Future | None = None
        self.scan_task: Future | None = None
        self.disconnect_monitor_task: asyncio.Task | None = None

        self.latest_led_values = [0] * len(self.slider_keys)
        self.values_lock = threading.Lock()

        # Store the main thread's ID for safe UI updates
        self.main_thread_id = threading.get_ident()

        # Build the UI
        # Create top container first
        self.top_container = ttk.Frame(self)
        self.top_container.pack(fill='x', padx=10, pady=5)

        # Build scan frame and mode selector (which will both use the top container)
        self._build_scan_frame()
        self._build_mode_selector_frame()

        self.color_preview_canvas = None
        self.color_preview_dimmed_canvas = None
        self._build_rgb_color_preview_frame()

        # Build the rest of the UI
        self._build_raw_control_frame()
        self._build_rgb_control_frame()
        self._build_aqi_control_frame()
        self._build_log_frame()
        self._sliders_disable()

        # Ensure graceful shutdown
        self.protocol("WM_DELETE_WINDOW", self.on_closing)

        # --- Final Initialization ---
        # Check for Bluetooth adapter availability after the UI is built.
        self._on_btn_check_ble()

    # --- UI Building Methods ---
    def _build_mode_selector_frame(self):
        # Create a container frame to hold both the scan frame and mode selector side by side
        if not hasattr(self, 'top_container'):
            self.top_container = ttk.Frame(self)
            self.top_container.pack(fill='x', padx=10, pady=5)

        # Create a frame for control mode
        right_side_frame = ttk.Frame(self.top_container)
        right_side_frame.pack(side='left', fill='y', padx=5, pady=5)

        # LED Control Mode frame
        frame = ttk.LabelFrame(right_side_frame, text="LED Control Mode")
        frame.pack(fill='both', expand=True, padx=5, pady=5)

        self.control_mode = tk.StringVar(value="raw")

        # Radio buttons for control modes - store references to disable when not connected
        self.mode_radiobuttons = []

        rb_raw = ttk.Radiobutton(frame, text="Use Raw Current/PWM LED Controls", 
                       variable=self.control_mode, value="raw",
                       command=self._on_control_mode_change)
        rb_raw.pack(anchor="w", padx=10, pady=2)
        self.mode_radiobuttons.append(rb_raw)

        rb_rgb = ttk.Radiobutton(frame, text="Use RGB+Brightness Controls", 
                       variable=self.control_mode, value="rgb",
                       command=self._on_control_mode_change)
        rb_rgb.pack(anchor="w", padx=10, pady=2)
        self.mode_radiobuttons.append(rb_rgb)

        rb_aqi = ttk.Radiobutton(frame, text="Use AQI+Brightness Controls",
                       variable=self.control_mode, value="aqi",
                       command=self._on_control_mode_change)
        rb_aqi.pack(anchor="w", padx=10, pady=2)
        self.mode_radiobuttons.append(rb_aqi)

        rb_manual_brightness_ctrl = ttk.Radiobutton(frame, text="Manual Brightness Control",
                                 variable=self.control_mode, value="manual_brightness_ctrl",
                                 command=self._on_control_mode_change)
        rb_manual_brightness_ctrl.pack(anchor="w", padx=10, pady=2)
        self.mode_radiobuttons.append(rb_manual_brightness_ctrl)

        input_frame = ttk.Frame(frame)
        input_frame.pack(fill='x', padx=10, pady=5)

        self.text_entry_led_brightness = ttk.Entry(input_frame, width=15)
        self.text_entry_led_brightness.pack(side='left', padx=(0, 5))

        self.button_led_brightness_set = ttk.Button(input_frame, text="Set",
                                                    command=self._on_set_manual_brightness_button_click)
        self.button_led_brightness_set.pack(side='right')

        self.static_text_label1 = ttk.Label(frame, text="Use: off | night | day | bright_day")
        self.static_text_label1.pack(anchor="w", padx=10, pady=(0, 5))

        self.static_text_label2 = ttk.Label(frame, text="or 0-100% | 0.0-100.0%")
        self.static_text_label2.pack(anchor="w", padx=10, pady=(0, 5))

        # Initially disable mode selection since we're not connected
        self._set_led_control_frame_state(False)

    def _build_rgb_color_preview_frame(self):
        # Create a frame to display the current RGB color
        frame = ttk.LabelFrame(self.top_container, text="Current Color")
        frame.pack(side='right', fill='y', padx=5, pady=5)

        # Create main container with horizontal layout
        main_frame = ttk.Frame(frame)
        main_frame.pack(fill='x', padx=10, pady=10)

        # Create canvas container for both color previews
        canvas_frame = ttk.Frame(main_frame)
        canvas_frame.pack(side='left', padx=(0, 20))

        # Original color canvas container
        original_canvas_frame = ttk.Frame(canvas_frame)
        original_canvas_frame.pack(side='left', padx=(0, 10))
        ttk.Label(original_canvas_frame, text="Original", font=('TkDefaultFont', 8, 'bold')).pack()
        self.color_preview_canvas = tk.Canvas(original_canvas_frame, width=100, height=100, background=StatusColor.GREY.value)
        self.color_preview_canvas.pack()

        # Dimmed color canvas container
        dimmed_canvas_frame = ttk.Frame(canvas_frame)
        dimmed_canvas_frame.pack(side='left')
        ttk.Label(dimmed_canvas_frame, text="Dimmed (HSV -> PWM)", font=('TkDefaultFont', 8, 'bold')).pack()
        self.color_preview_dimmed_canvas = tk.Canvas(dimmed_canvas_frame, width=100, height=100, background=StatusColor.GREY.value)
        self.color_preview_dimmed_canvas.pack()

        # Create text fields for different color formats on the right
        info_frame = ttk.Frame(main_frame)
        info_frame.pack(side='left', fill='both', expand=True)

        # Headers for original and dimmed values
        ttk.Label(info_frame, text="Original", font=('TkDefaultFont', 8, 'bold')).grid(row=0, column=1, sticky='w', pady=2)
        ttk.Label(info_frame, text="Dimmed (HSV -> PWM)", font=('TkDefaultFont', 8, 'bold')).grid(row=0, column=2, sticky='w', padx=(10, 0), pady=2)

        # RGB decimal format
        ttk.Label(info_frame, text="RGB:").grid(row=1, column=0, sticky='w', padx=(0, 5), pady=2)
        self.rgb_decimal_label = ttk.Label(info_frame, text="")
        self.rgb_decimal_label.grid(row=1, column=1, sticky='w', pady=2)
        self.rgb_decimal_dimmed_label = ttk.Label(info_frame, text="")
        self.rgb_decimal_dimmed_label.grid(row=1, column=2, sticky='w', padx=(10, 0), pady=2)

        # RGB hex format
        ttk.Label(info_frame, text="RGB:").grid(row=2, column=0, sticky='w', padx=(0, 5), pady=2)
        self.rgb_hex_label = ttk.Label(info_frame, text="")
        self.rgb_hex_label.grid(row=2, column=1, sticky='w', pady=2)
        self.rgb_hex_dimmed_label = ttk.Label(info_frame, text="")
        self.rgb_hex_dimmed_label.grid(row=2, column=2, sticky='w', padx=(10, 0), pady=2)

        # HSL format
        ttk.Label(info_frame, text="HSL:").grid(row=3, column=0, sticky='w', padx=(0, 5), pady=2)
        self.hsl_label = ttk.Label(info_frame, text="")
        self.hsl_label.grid(row=3, column=1, sticky='w', pady=2)
        self.hsl_dimmed_label = ttk.Label(info_frame, text="")
        self.hsl_dimmed_label.grid(row=3, column=2, sticky='w', padx=(10, 0), pady=2)

        # HSV format
        ttk.Label(info_frame, text="HSV:").grid(row=4, column=0, sticky='w', padx=(0, 5), pady=2)
        self.hsv_label = ttk.Label(info_frame, text="")
        self.hsv_label.grid(row=4, column=1, sticky='w', pady=2)
        self.hsv_dimmed_label = ttk.Label(info_frame, text="")
        self.hsv_dimmed_label.grid(row=4, column=2, sticky='w', padx=(10, 0), pady=2)

    def _build_scan_frame(self):
        # Create a container frame to hold both the scan frame and mode selector side by side
        if not hasattr(self, 'top_container'):
            self.top_container = ttk.Frame(self)
            self.top_container.pack(fill='x', padx=10, pady=5)

        frame = ttk.LabelFrame(self.top_container, text="Device Scan & Connect")
        frame.pack(side='left', fill='y', padx=5, pady=5, ipady=5)
        self.btn_start = ttk.Button(frame, text="Start Scan", command=self.start_scan, state='disabled')
        self.btn_start.grid(row=0, column=0, padx=5, pady=5)
        self.btn_stop = ttk.Button(frame, text="Stop Scan", command=self.stop_scan, state='disabled')
        self.btn_stop.grid(row=0, column=1, padx=5, pady=5)

        self.btn_check_ble = ttk.Button(frame, text="Check Bluetooth adapter",
                                        command=self._on_btn_check_ble, state='disabled')
        self.btn_check_ble.grid(row=0, column=2, padx=5, pady=5)

        list_frame = ttk.Frame(frame)
        list_frame.grid(row=1, column=0, columnspan=5, pady=5, sticky='we')
        self.device_listbox = tk.Listbox(list_frame, height=5, width=45)
        scrollbar = ttk.Scrollbar(list_frame, orient='vertical', command=self.device_listbox.yview)
        self.device_listbox.config(yscrollcommand=scrollbar.set)
        self.device_listbox.pack(side='left', fill='both', expand=True)
        scrollbar.pack(side='right', fill='y')
        self.device_listbox.bind('<<ListboxSelect>>', self.on_device_selected)
        self.btn_connect = ttk.Button(frame, text="Connect", state='disabled', command=self.connect_device)
        self.btn_connect.grid(row=2, column=0, padx=5, pady=5)
        self.btn_disconnect = ttk.Button(frame, text="Disconnect", state='disabled', command=self.disconnect_device)
        self.btn_disconnect.grid(row=2, column=1, padx=5, pady=5)
        self.status_canvas = tk.Canvas(frame, width=20, height=20)
        self.status_oval = self.status_canvas.create_oval(2, 2, 18, 18, fill=StatusColor.GREY.value)
        self.status_canvas.grid(row=2, column=2, padx=5)

    def _build_raw_control_frame(self):
        frame = ttk.LabelFrame(self, text="Raw Current/PWM LED Controls")
        frame.pack(fill='x', expand=True, padx=10, pady=5, ipady=5)
        frame.columnconfigure(0, weight=1)
        self.control_frame = frame

        self.sliders = {}
        color_params = {'Red': ('R_current', 'R_PWM'), 'Green': ('G_current', 'G_PWM'), 'Blue': ('B_current', 'B_PWM')}
        for idx, (color, keys) in enumerate(color_params.items()):
            cf = ttk.LabelFrame(frame, text=color)
            cf.grid(row=idx, column=0, padx=5, pady=5, sticky='we')
            cf.columnconfigure(1, weight=1)
            for jdx, key in enumerate(keys):
                label_text = 'Current' if 'current' in key else 'PWM'
                ttk.Label(cf, text=label_text, width=LABEL_WIDTH).grid(row=jdx, column=0, padx=5, pady=2, sticky='w')
                scale = ttk.Scale(cf, from_=0, to=255, orient='horizontal',
                                  command=lambda v, k=key: self._on_slider_change_raw_current_pwm(k, v))
                scale.grid(row=jdx, column=1, padx=5, pady=2, sticky='we')
                value_label = ttk.Label(cf, text="0", width=4)
                value_label.grid(row=jdx, column=2, padx=5, pady=2)
                self.sliders[key] = (scale, value_label)

    def _build_rgb_control_frame(self):
        frame = ttk.LabelFrame(self, text="RGB Control")
        frame.pack(fill='x', padx=10, pady=5, ipady=5)
        frame.columnconfigure(1, weight=1)
        self.rgb_frame = frame

        self.rgb_sliders = {}
        colors = {'Red': 'R', 'Green': 'G', 'Blue': 'B'}

        for idx, (label_text, key) in enumerate(colors.items()):
            ttk.Label(frame, text=label_text, width=LABEL_WIDTH).grid(row=idx, column=0, padx=5, pady=2, sticky='w')
            scale = ttk.Scale(frame, from_=0, to=255, orient='horizontal',
                              command=lambda v, k=key: self._on_slider_change_rgb(k, v))
            scale.grid(row=idx, column=1, padx=5, pady=2, sticky='we')
            value_label = ttk.Label(frame, text="0", width=4)
            value_label.grid(row=idx, column=2, padx=5, pady=2)
            self.rgb_sliders[key] = (scale, value_label)

        # Brightness sliders start after RGB sliders (row 3 and 4)
        ttk.Label(frame, text="Brightness: LED current", width=LABEL_WIDTH).grid(row=3, column=0, padx=5, pady=2, sticky='w')
        self.brightness_label = ttk.Label(frame, text="{}".format(self.brightness), width=4)
        self.brightness_label.grid(row=3, column=2, padx=5, pady=2)
        self.brightness_scale = ttk.Scale(frame, from_=0, to=100, orient='horizontal',
                                          command=self._on_brightness_slider_change)
        self.brightness_scale.grid(row=3, column=1, padx=5, pady=2, sticky='we')
        self.brightness_scale.set(self.brightness)  # Default to 20%

        # PWM brightness slider
        ttk.Label(frame, text="Brightness: HSV -> PWM", width=LABEL_WIDTH).grid(row=4, column=0, padx=5, pady=2, sticky='w')
        self.hsv_pwm_brightness_label = ttk.Label(frame, text="{}".format(self.hsv_brightness), width=4)
        self.hsv_pwm_brightness_label.grid(row=4, column=2, padx=5, pady=2)
        self.hsv_pwm_brightness_scale = ttk.Scale(frame, from_=0, to=100, orient='horizontal',
                                                  command=self._on_hsv_pwm_brightness_slider_change)
        self.hsv_pwm_brightness_scale.grid(row=4, column=1, padx=5, pady=2, sticky='we')
        self.hsv_pwm_brightness_scale.set(self.hsv_brightness)  # Default to 100%

    def _build_aqi_control_frame(self):
        frame = ttk.LabelFrame(self, text="AQI Control")
        frame.pack(fill='x', padx=10, pady=5, ipady=5)
        frame.columnconfigure(1, weight=1)
        self.aqi_frame = frame

        ttk.Label(frame, text="AQI").grid(row=0, column=0, padx=5, pady=2, sticky='w')
        self.aqi_scale = ttk.Scale(frame, from_=0, to=100, orient='horizontal',
                                   command=self._on_aqi_slider_change_aqi)
        self.aqi_scale.grid(row=0, column=1, padx=5, pady=2, sticky='we')
        self.aqi_value_label = ttk.Label(frame, text="0.0", width=4)
        self.aqi_value_label.grid(row=0, column=2, padx=5, pady=2)

        # Brightness presets radio buttons
        ttk.Label(frame, text="Brightness:").grid(row=1, column=0, padx=5, pady=2, sticky='w')

        # Create frame for radio buttons and brightness controls in the same row
        brightness_controls_frame = ttk.Frame(frame)
        brightness_controls_frame.grid(row=1, column=1, columnspan=2, padx=5, pady=2, sticky='we')
        brightness_controls_frame.columnconfigure(1, weight=1)

        # Create frame for radio buttons
        radio_frame = ttk.Frame(brightness_controls_frame)
        radio_frame.grid(row=0, column=0, sticky='w')

        self.brightness_preset_frame = radio_frame

        # Create StringVar for radio button selection
        self.brightness_preset = tk.StringVar(value="Day")

        # Create radio buttons
        ttk.Radiobutton(radio_frame, text="Night", variable=self.brightness_preset,
                        value="Night",
                        command=self._on_brightness_preset_change).pack(side='left', padx=(0, 10))
        ttk.Radiobutton(radio_frame, text="Day", variable=self.brightness_preset,
                        value="Day",
                        command=self._on_brightness_preset_change).pack(side='left', padx=(0, 10))
        ttk.Radiobutton(radio_frame, text="BrightDay", variable=self.brightness_preset,
                        value="BrightDay",
                        command=self._on_brightness_preset_change).pack(side='left', padx=(0, 10))
        ttk.Radiobutton(radio_frame, text="Percent", variable=self.brightness_preset,
                        value="Percent",
                        command=self._on_brightness_preset_change).pack(side='left', padx=(0, 10))
        # Brightness scale and label (only visible when "Percent" is selected)
        self.aqi_brightness = 50
        self.aqi_brightness_scale = ttk.Scale(brightness_controls_frame, from_=0.0, to=100.0, orient='horizontal',
                                              command=self._on_aqi_brightness_slider_change)
        self.aqi_brightness_scale.grid(row=0, column=1, padx=10, pady=2, sticky='we')
        self.aqi_brightness_value_label = ttk.Label(brightness_controls_frame, text="0.0", width=4)
        self.aqi_brightness_value_label.grid(row=0, column=2, padx=5, pady=2)
        # Initially hide the brightness scale and label since "Day" is default
        self.aqi_brightness_scale.grid_remove()
        self.aqi_brightness_value_label.grid_remove()

    def _build_log_frame(self):
        frame = ttk.LabelFrame(self, text="Log")
        frame.pack(fill='both', expand=True, padx=10, pady=5, ipady=5)
        self.log_text = tk.Text(frame, height=10, state='disabled')
        log_scroll = ttk.Scrollbar(frame, orient='vertical', command=self.log_text.yview)
        self.log_text.config(yscrollcommand=log_scroll.set)
        self.log_text.pack(side='left', fill='both', expand=True)
        log_scroll.pack(side='right', fill='y')

    # --- Core Application Logic ---

    def on_closing(self):
        if self.client:
            self.log("Disconnecting before exit...", LogLevel.INF)
            if self.disconnect_monitor_task and not self.disconnect_monitor_task.done():
                self.disconnect_monitor_task.cancel()
            future = asyncio.run_coroutine_threadsafe(self._async_disconnect(manual=False), self.async_loop)
            try:
                future.result(timeout=2.0)
            except Exception as e:
                self.log(f"Error during shutdown disconnect: {e}", LogLevel.ERR)
        self.async_loop.call_soon_threadsafe(self.async_loop.stop)
        self.destroy()

    def log(self, message: str, level: LogLevel = LogLevel.INF):
        if level.value < self.log_level.value:
            return

        origin_thread_id = threading.get_ident()

        def _append_log():
            timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]

            scroll_pos = self.log_text.yview()
            is_at_bottom = scroll_pos[1] >= 1.0

            self.log_text.config(state='normal')
            self.log_text.insert('end', f"[{level.name}] [{origin_thread_id}] [{timestamp}] {message}\n")

            lines = int(self.log_text.index('end-1c').split('.')[0])
            if lines > MAX_LOG_LINES:
                self.log_text.delete('1.0', f"{lines - MAX_LOG_LINES}.0")

            if is_at_bottom:
                self.log_text.see('end')

            self.log_text.config(state='disabled')

        self.after(0, _append_log)

    # --- Thread-Safe UI Helpers ---
    def _btn_set_state(self, btn: ttk.Button, enabled: bool):
        """Thread-safely enables or disables a button."""
        state = 'normal' if enabled else 'disabled'
        if threading.get_ident() == self.main_thread_id:
            btn.config(state=state)
        else:
            self.after(0, btn.config, {'state': state})

    def _btn_start_enable(self):
        self._btn_set_state(self.btn_start, True)

    def _btn_start_disable(self):
        self._btn_set_state(self.btn_start, False)

    def _btn_stop_enable(self):
        self._btn_set_state(self.btn_stop, True)

    def _btn_stop_disable(self):
        self._btn_set_state(self.btn_stop, False)

    def _btn_connect_enable(self):
        self._btn_set_state(self.btn_connect, True)

    def _btn_connect_disable(self):
        self._btn_set_state(self.btn_connect, False)

    def _btn_disconnect_enable(self):
        self._btn_set_state(self.btn_disconnect, True)

    def _btn_disconnect_disable(self):
        self._btn_set_state(self.btn_disconnect, False)

    def _btn_check_ble_enable(self):
        self._btn_set_state(self.btn_check_ble, True)

    def _btn_check_ble_disable(self):
        self._btn_set_state(self.btn_check_ble, False)

    def _device_listbox_clear(self):
        """Thread-safely clears the device listbox."""
        if threading.get_ident() == self.main_thread_id:
            self.device_listbox.delete(0, 'end')
        else:
            self.after(0, self.device_listbox.delete, 0, 'end')

    def _set_sliders_state(self, enabled: bool):
        # This is now a wrapper that sets the state for all control frames
        # based on connection state
        if enabled:
            # When connected, only enable the active control mode
            self._update_control_frames_state()
        else:
            # When disconnected, disable all controls
            self._set_raw_controls_state(False)
            self._set_rgb_controls_state(False)
            self._set_aqi_controls_state(False)

    def _set_raw_controls_state(self, enabled: bool):
        state = 'normal' if enabled else 'disabled'
        # Raw control frame
        for widget in self.control_frame.winfo_children():
            if isinstance(widget, ttk.LabelFrame):
                for child in widget.winfo_children():
                    if isinstance(child, (ttk.Scale, ttk.Label)):
                        child.configure(state=state)

    def _set_rgb_controls_state(self, enabled: bool):
        state = 'normal' if enabled else 'disabled'
        # RGB+Brightness frame
        for widget in self.rgb_frame.winfo_children():
            if isinstance(widget, (ttk.Scale, ttk.Label)):
                widget.configure(state=state)

    def _set_aqi_controls_state(self, enabled: bool):
        state = 'normal' if enabled else 'disabled'

        # AQI frame
        for widget in self.aqi_frame.winfo_children():
            if isinstance(widget, (ttk.Scale, ttk.Label)):
                widget.configure(state=state)
        for widget in self.brightness_preset_frame.winfo_children():
            if isinstance(widget, ttk.Radiobutton):
                widget.configure(state=state)

    def _set_led_control_frame_state(self, enabled: bool):
        """Enable or disable the mode selection radio buttons."""
        state = 'normal' if enabled else 'disabled'
        for radiobutton in self.mode_radiobuttons:
            radiobutton.configure(state=state)
        self._set_manual_brightness_ctrl_state(enabled)

    def _set_manual_brightness_ctrl_state(self, enabled: bool):
        if enabled and self.control_mode.get() == 'manual_brightness_ctrl':
            self.text_entry_led_brightness.configure(state='normal')
            self.button_led_brightness_set.configure(state='normal')
        else:
            self.text_entry_led_brightness.configure(state='disabled')
            self.button_led_brightness_set.configure(state='disabled')

    def _sliders_enable(self):
        self._set_sliders_state(True)

    def _sliders_disable(self):
        self._set_sliders_state(False)

    def _update_status_circle(self, color: StatusColor):
        """Thread-safely updates the color of the status indicator."""
        if threading.get_ident() == self.main_thread_id:
            self.status_canvas.itemconfig(self.status_oval, fill=color.value)
        else:
            self.after(0, lambda: self.status_canvas.itemconfig(self.status_oval, fill=color.value))

    def _update_hsv_dimmed(self):
        """Update the dimmed RGB values based on HSV value percentage."""
        h, s, v = rgb_to_hsv(self.rgb_values['R'], self.rgb_values['G'], self.rgb_values['B'])
        v = v * self.hsv_brightness / 100.0
        r, g, b = hsv_to_rgb(h, s, v)
        self.rgb_dimmed['R'] = r
        self.rgb_dimmed['G'] = g
        self.rgb_dimmed['B'] = b

    def _reset_current_color_canvases_and_info(self):
        self.color_preview_canvas.config(background=StatusColor.GREY.value)
        self.color_preview_dimmed_canvas.config(background=StatusColor.GREY.value)
        self.rgb_decimal_label.config(text='')
        self.rgb_decimal_dimmed_label.config(text='')
        self.rgb_hex_label.config(text='')
        self.rgb_hex_dimmed_label.config(text='')
        self.hsl_label.config(text='')
        self.hsl_dimmed_label.config(text='')
        self.hsv_label.config(text='')
        self.hsv_dimmed_label.config(text='')


    def _update_color_preview(self):
        """Updates the color preview based on current mode and RGB values."""
        mode = self.control_mode.get()

        if mode == "raw":
            # In raw mode, display grey and reset text fields
            color_original = StatusColor.GREY.value
            color_dimmed = StatusColor.GREY.value
            rgb_decimal = ""
            rgb_hex = ""
            hsl = ""
            hsv = ""
            rgb_decimal_dimmed = ""
            rgb_hex_dimmed = ""
            hsl_dimmed = ""
            hsv_dimmed = ""
        else:
            # In RGB or AQI mode, display the current RGB color
            r = self.rgb_values['R']
            g = self.rgb_values['G']
            b = self.rgb_values['B']

            # Update dimmed values
            self._update_hsv_dimmed()
            r_dimmed = self.rgb_dimmed['R']
            g_dimmed = self.rgb_dimmed['G']
            b_dimmed = self.rgb_dimmed['B']

            # Convert RGB to hex color codes for both canvases
            color_original = f'#{r:02x}{g:02x}{b:02x}'
            color_dimmed = f'#{r_dimmed:02x}{g_dimmed:02x}{b_dimmed:02x}'

            # Prepare text field values for original
            rgb_decimal = f"{r}, {g}, {b}"
            rgb_hex = f'#{r:02x}{g:02x}{b:02x}'
            h, s, l = rgb_to_hsl(r, g, b)
            hsl = f"{h:.0f}, {s:.0f}, {l:.0f}"
            h, s, v = rgb_to_hsv(r, g, b)
            hsv = f"{h:.0f}, {s:.0f}, {v:.0f}"

            # Prepare text field values for dimmed
            rgb_decimal_dimmed = f"{r_dimmed}, {g_dimmed}, {b_dimmed}"
            rgb_hex_dimmed = f'#{r_dimmed:02x}{g_dimmed:02x}{b_dimmed:02x}'
            h_dimmed, s_dimmed, l_dimmed = rgb_to_hsl(r_dimmed, g_dimmed, b_dimmed)
            hsl_dimmed = f"{h_dimmed:.0f}, {s_dimmed:.0f}, {l_dimmed:.0f}"
            h_dimmed, s_dimmed, v_dimmed = rgb_to_hsv(r_dimmed, g_dimmed, b_dimmed)
            hsv_dimmed = f"{h_dimmed:.0f}, {s_dimmed:.0f}, {v_dimmed:.0f}"

        def update_ui():
            self.color_preview_canvas.config(background=color_original)
            self.color_preview_dimmed_canvas.config(background=color_dimmed)
            self.rgb_decimal_label.config(text=rgb_decimal)
            self.rgb_hex_label.config(text=rgb_hex)
            self.hsl_label.config(text=hsl)
            self.hsv_label.config(text=hsv)
            self.rgb_decimal_dimmed_label.config(text=rgb_decimal_dimmed)
            self.rgb_hex_dimmed_label.config(text=rgb_hex_dimmed)
            self.hsl_dimmed_label.config(text=hsl_dimmed)
            self.hsv_dimmed_label.config(text=hsv_dimmed)

        if threading.get_ident() == self.main_thread_id:
            update_ui()
        else:
            self.after(0, update_ui)

    # --- UI Actions ---
    def start_scan(self):
        self.log("Start Scan button pressed.", LogLevel.DBG)
        self._btn_start_disable()
        self._btn_check_ble_disable()
        self._btn_connect_disable()
        self.scan_task = asyncio.run_coroutine_threadsafe(self._async_scan(), self.async_loop)

    def stop_scan(self):
        self._btn_stop_disable()
        self.scanning = False
        self.log("Stopping scanning...", LogLevel.INF)

    def on_device_selected(self, event):
        if self.device_listbox.curselection() and not self.client:
            self._btn_connect_enable()

    def connect_device(self):
        try:
            selection = self.device_listbox.get(self.device_listbox.curselection())
            address = self.discovered_devices.get(selection)
            if not address:
                self.log("Could not find address for selected device.", LogLevel.ERR)
                return
            self.log(f"Connect button pressed for {address}.", LogLevel.INF)
            self._btn_start_disable()
            self._btn_check_ble_disable()
            self._btn_stop_disable()
            self._btn_connect_disable()
            self._btn_disconnect_disable()
            asyncio.run_coroutine_threadsafe(self._async_stop_scan_and_connect(address), self.async_loop)
        except tk.TclError:
            self.log("No device selected.", LogLevel.WRN)

    def disconnect_device(self):
        self.log("Disconnecting...", LogLevel.INF)
        self._sliders_disable()
        self._btn_disconnect_disable()
        if threading.get_ident() == self.main_thread_id:
            self._reset_current_color_canvases_and_info()
        else:
            self.after(0, self._reset_current_color_canvases_and_info)
        asyncio.run_coroutine_threadsafe(self._async_disconnect(), self.async_loop)

    def _on_slider_change_raw_current_pwm(self, param: str, value: str | int):
        _, label = self.sliders[param]
        int_value = int(float(value))
        label.config(text=str(int_value))

        with self.values_lock:
            try:
                index = self.slider_keys.index(param)
                self.latest_led_values[index] = int_value
            except ValueError:
                return

        if self.send_task and not self.send_task.done():
            return

        self.send_task = asyncio.run_coroutine_threadsafe(self._async_send_command(), self.async_loop)

    def _on_control_mode_change(self):
        self._update_control_frames_state()

    def _on_set_manual_brightness_button_click(self):
        brightness_value = self.text_entry_led_brightness.get()
        self.send_task = asyncio.run_coroutine_threadsafe(self._async_send_manual_brightness_ctrl(brightness_value), self.async_loop)

    def _update_control_frames_state(self):
        mode = self.control_mode.get()

        # Only enable controls if we're connected
        is_connected = self.client is not None

        self._set_manual_brightness_ctrl_state(True if mode == 'manual_brightness_ctrl' else False)

        if is_connected:
            # Enable/disable controls based on selected mode
            raw_enabled = mode == "raw"
            rgb_enabled = mode == "rgb"
            aqi_enabled = mode == "aqi"
            brightness_enabled = mode in ["rgb"]

            # Set the state of each control group directly
            self._set_raw_controls_state(raw_enabled)
            self._set_rgb_controls_state(rgb_enabled)
            self._set_aqi_controls_state(aqi_enabled)

            # Update internal state based on current mode
            # (No UI updates needed here - just synchronize internal values)
            if mode == "rgb":
                # Sync internal state from RGB values
                self._on_slider_change_rgb('R', self.rgb_values['R'])
                self._on_slider_change_rgb('G', self.rgb_values['G'])
                self._on_slider_change_rgb('B', self.rgb_values['B'])
                self._on_brightness_slider_change(self.brightness)
            elif mode == "aqi":
                self._on_aqi_slider_change_aqi(self.aqi_value)
                self._on_brightness_preset_change()
        else:
            # When disconnected, all controls are disabled
            self._set_raw_controls_state(False)
            self._set_rgb_controls_state(False)
            self._set_aqi_controls_state(False)

    def _set_slider_raw_current_pwm(self, key: str, value: int):
        try:
            index = self.slider_keys.index(key)
            with self.values_lock:
                self.latest_led_values[index] = value
        except ValueError:
            pass
        try:
            scale, label = self.sliders[key]
            self._set_raw_controls_state(True)
            scale.set(value)
            label.config(text=str(value))
            self._set_raw_controls_state(False)
        except tk.TclError:
            # Widget might be disabled or destroyed
            self.log(f"Could not update slider", LogLevel.DBG)
        self._on_slider_change_raw_current_pwm(key, value)

    def _on_slider_change_rgb(self, component: str, value: str | int):
        """
        :param component: The RGB component ('R', 'G', or 'B')
        :param value: The new value for the component
        :return: None
        """
        int_value = int(float(value))
        _, label = self.rgb_sliders[component]
        label.config(text=str(int_value))

        self.rgb_values[component] = int_value

        # Update internal state without UI interactions
        # Map RGB values to raw slider values
        pwm_mapping = {'R': 'R_PWM', 'G': 'G_PWM', 'B': 'B_PWM'}

        pwm_key = pwm_mapping.get(component)
        if pwm_key:
            try:
                index = self.slider_keys.index(pwm_key)
                with self.values_lock:
                    self.latest_led_values[index] = int_value
            except ValueError:
                pass
            try:
                scale, label = self.sliders[pwm_key]
                self._set_raw_controls_state(True)
                scale.set(int_value)
                label.config(text=str(int_value))
                self._set_raw_controls_state(False)
            except tk.TclError:
                # Widget might be disabled or destroyed
                self.log(f"Could not update slider", LogLevel.DBG)

        # Update dimmed RGB values
        self._update_hsv_dimmed()

        # Update color preview
        self._update_color_preview()

        # Send the command
        if self.send_task and not self.send_task.done():
            return

        self.send_task = asyncio.run_coroutine_threadsafe(self._async_send_command(), self.async_loop)

    def _on_hsv_pwm_brightness_slider_change(self, value):
        # Convert to integer (0-100 range)
        int_value = int(float(value))
        self.hsv_pwm_brightness_label.config(text=str(int_value))

        # Update PWM brightness value
        self.hsv_brightness = int_value

        # Update dimmed RGB values
        self._update_hsv_dimmed()

        # Update color preview
        self._update_color_preview()

        # Send the command if we're in RGB or AQI mode
        mode = self.control_mode.get()
        if mode in ["rgb", "aqi"]:
            if self.send_task and not self.send_task.done():
                return
            self.send_task = asyncio.run_coroutine_threadsafe(self._async_send_command(), self.async_loop)

    def _update_raw_controls_from_rgb(self):
        # This method is maintained for backward compatibility but should not be used
        # Internal state is now updated directly in _on_rgb_slider_change
        self.log("This method is deprecated, use _on_rgb_slider_change instead", LogLevel.DBG)
        pass

    def _on_aqi_slider_change_aqi(self, value):
        # Convert to float with one decimal place
        float_value = round(float(value), 1)
        self.aqi_value = float_value
        if hasattr(self, 'aqi_value_label') and self.aqi_value_label is not None:
            self.aqi_value_label.config(text=f"{float_value:.1f}")
        brightness = self.brightness_preset.get()

        if self.aqi_value > 89.5:
            color = AQI_COLORS[brightness]['Excellent']
        elif self.aqi_value > 79.5:
            color = AQI_COLORS[brightness]['Good']
        elif self.aqi_value > 49.5:
            color = AQI_COLORS[brightness]['Fair']
        elif self.aqi_value > 9.5:
            color = AQI_COLORS[brightness]['Poor']
        else:
            color = AQI_COLORS[brightness]['VeryPoor']

        with self.values_lock:
            try:
                self.latest_led_values[self.slider_keys.index('R_PWM')] = color['R']
                self.latest_led_values[self.slider_keys.index('G_PWM')] = color['G']
                self.latest_led_values[self.slider_keys.index('B_PWM')] = color['B']
            except ValueError:
                raise

        try:
            scale_r, label_r = self.rgb_sliders['R']
            scale_g, label_g = self.rgb_sliders['G']
            scale_b, label_b = self.rgb_sliders['B']
            scale_r_pwm, label_r_pwm = self.sliders['R_PWM']
            scale_g_pwm, label_g_pwm = self.sliders['G_PWM']
            scale_b_pwm, label_b_pwm = self.sliders['B_PWM']

            self._set_rgb_controls_state(True)

            self.rgb_values['R'] = color['R']
            self.rgb_values['G'] = color['G']
            self.rgb_values['B'] = color['B']
            scale_r.set(color['R'])
            scale_g.set(color['G'])
            scale_b.set(color['B'])
            label_r.config(text=str(color['R']))
            label_g.config(text=str(color['G']))
            label_b.config(text=str(color['B']))
            scale_r_pwm.set(color['R'])
            scale_g_pwm.set(color['G'])
            scale_b_pwm.set(color['B'])
            label_r_pwm.config(text=str(color['R']))
            label_g_pwm.config(text=str(color['G']))
            label_b_pwm.config(text=str(color['B']))

            self._set_rgb_controls_state(False)
        except tk.TclError:
            # Widget might be disabled or destroyed
            self.log(f"Could not update slider", LogLevel.DBG)

        # Update dimmed RGB values
        self._update_hsv_dimmed()
        # Update color preview
        self._update_color_preview()

        if self.send_task and not self.send_task.done():
            return
        self.send_task = asyncio.run_coroutine_threadsafe(self._async_send_command(), self.async_loop)

    @staticmethod
    def _conv_aqi_brightness_to_brightness(aqi_brightness: float) -> int:
        if aqi_brightness < AQI_BRIGHTNESS_MIN_PERCENT:
            return BRIGHTNESS_MIN
        return int(round(BRIGHTNESS_MIN + (float(aqi_brightness - AQI_BRIGHTNESS_MIN_PERCENT) *
                                           BRIGHTNESS_RANGE / AQI_BRIGHTNESS_RANGE_PERCENT)))

    @staticmethod
    def _conv_brightness_to_aqi_brightness(brightness: int) -> float:
        return AQI_BRIGHTNESS_MIN_PERCENT + (brightness - BRIGHTNESS_MIN) * AQI_BRIGHTNESS_RANGE_PERCENT / BRIGHTNESS_RANGE

    def _on_brightness_preset_change(self):
        """Handle brightness preset radio button changes."""
        preset = self.brightness_preset.get()

        if preset == "Percent":
            # Show the brightness scale and label
            self.aqi_brightness_scale.grid()
            self.aqi_brightness_value_label.grid()
            self.aqi_brightness_scale.set(self.aqi_brightness)
        else:
            # Hide the brightness scale and label
            self.aqi_brightness_scale.grid_remove()
            self.aqi_brightness_value_label.grid_remove()
            self.hsv_pwm_brightness_scale.configure(state="normal")
            self.hsv_pwm_brightness_scale.set(100)
            self.hsv_pwm_brightness_scale.configure(state="disabled")

        if preset == "Night":
            currents = CURRENTS[preset]
            self.aqi_brightness = self._conv_brightness_to_aqi_brightness(BRIGHTNESS_NIGHT)
        elif preset == "Day":
            currents = CURRENTS[preset]
            self.aqi_brightness = self._conv_brightness_to_aqi_brightness(BRIGHTNESS_DAY)
        elif preset == "BrightDay":
            currents = CURRENTS[preset]
            self.aqi_brightness = self._conv_brightness_to_aqi_brightness(BRIGHTNESS_BRIGHT_DAY)
        elif preset == "Percent":
            brightness_percent = self._conv_aqi_brightness_to_brightness(self.aqi_brightness_scale.get())
            currents = list(get_led_currents_for_brightness(brightness_percent))
        else:
            currents = [0, 0, 0]
        with self.values_lock:
            try:
                self.latest_led_values[self.slider_keys.index('R_current')] = currents[0]
                self.latest_led_values[self.slider_keys.index('G_current')] = currents[1]
                self.latest_led_values[self.slider_keys.index('B_current')] = currents[2]
            except ValueError:
                raise

        try:
            self._set_raw_controls_state(True)
            scale_r_current, label_r_current = self.sliders['R_current']
            scale_g_current, label_g_current = self.sliders['G_current']
            scale_b_current, label_b_current = self.sliders['B_current']
            scale_r_current.set(currents[0])
            scale_g_current.set(currents[1])
            scale_b_current.set(currents[2])
            label_r_current.config(text=str(currents[0]))
            label_g_current.config(text=str(currents[1]))
            label_b_current.config(text=str(currents[2]))
            self._set_raw_controls_state(False)
        except tk.TclError:
            # Widget might be disabled or destroyed
            self.log(f"Could not update slider", LogLevel.DBG)

        self._on_aqi_slider_change_aqi(self.aqi_value)

        if self.send_task and not self.send_task.done():
            return
        self.send_task = asyncio.run_coroutine_threadsafe(self._async_send_command(), self.async_loop)

    def _on_aqi_brightness_slider_change(self, value):
        float_value = round(float(value), 1)
        self.aqi_brightness = float_value
        self.aqi_brightness_value_label.config(text=f"{self.aqi_brightness:.1f}")
        self.brightness = self._conv_aqi_brightness_to_brightness(self.aqi_brightness)
        if self.aqi_brightness < AQI_BRIGHTNESS_MIN_PERCENT:
            pwm_dim = int(round(self.aqi_brightness * 100.0 / AQI_BRIGHTNESS_MIN_PERCENT))
        else:
            pwm_dim = 100

        self.brightness_scale.configure(state='normal')
        self.brightness_scale.set(self.brightness)
        self.brightness_scale.configure(state='disabled')
        self.hsv_pwm_brightness_scale.configure(state='normal')
        self.hsv_pwm_brightness_scale.set(pwm_dim)
        self.hsv_pwm_brightness_scale.configure(state='disabled')
        self._on_aqi_slider_change_aqi(self.aqi_value)

    def _on_brightness_slider_change(self, value):
        # Convert to integer (0-100 range)
        int_value = int(float(value))
        self.brightness_label.config(text=str(int_value))

        # Update common brightness value
        self.brightness = int_value

        # Update internal state without UI interactions
        current_keys = ['R_current', 'G_current', 'B_current']
        r_current, g_current, b_current = get_led_currents_for_brightness(int_value)

        for current_key, current_value in zip(current_keys, [r_current, g_current, b_current]):
            try:
                index = self.slider_keys.index(current_key)
                with self.values_lock:
                    self.latest_led_values[index] = int(current_value)
            except ValueError:
                pass
            try:
                scale, label = self.sliders[current_key]
                self._set_raw_controls_state(True)
                scale.set(current_value)
                label.config(text=str(current_value))
                self._set_raw_controls_state(False)
            except tk.TclError:
                # Widget might be disabled or destroyed
                self.log(f"Could not update slider", LogLevel.DBG)

        # Update color preview
        self._update_color_preview()

        # Send the command
        if self.send_task and not self.send_task.done():
            return

        self.send_task = asyncio.run_coroutine_threadsafe(self._async_send_command(), self.async_loop)

    def _update_raw_controls_from_aqi(self):
        # This method is maintained for backward compatibility but should not be used
        # Internal state is now updated directly in _on_aqi_slider_change
        self.log("This method is deprecated, use _on_aqi_slider_change instead", LogLevel.DBG)
        pass

    def _on_btn_check_ble(self):
        self._btn_check_ble_disable()
        asyncio.run_coroutine_threadsafe(self._async_on_btn_check_ble(), self.async_loop)

    # --- Asynchronous Methods ---

    async def _is_ble_available(self) -> bool:
        """A non-blocking check for a Bluetooth adapter."""
        try:
            await BleakScanner.discover(timeout=0.1)
            return True
        except BleakDBusError as e:
            # If a scan is already in progress, the adapter is clearly available.
            if 'org.bluez.Error.InProgress' in str(e):
                self.log("BLE check found an operation in progress; adapter is available.", LogLevel.DBG)
                return True
            self.log(f"BLE availability check failed with DBus error: {e}", LogLevel.ERR)
            return False
        except Exception as ex:
            self.log(f"BLE availability check failed: {ex}", LogLevel.ERR)
            return False

    async def _async_on_btn_check_ble(self):
        """Probes for a Bluetooth adapter and updates UI state accordingly."""
        self.log("Checking for Bluetooth adapter...", LogLevel.INF)
        if await self._is_ble_available():
            self.log("Bluetooth adapter found. Ready to scan.", LogLevel.INF)
            self._btn_start_enable()
            self._btn_stop_disable()
            self._btn_check_ble_disable()
            self._device_listbox_clear()
            self._btn_connect_disable()
            self._btn_disconnect_disable()
            self._sliders_disable()
        else:
            self.log("Bluetooth adapter not found or backend is not available.", LogLevel.ERR)
            self._update_ui_on_adapter_disappeared()

    async def _async_check_bluetooth_availability_on_disconnect(self):
        """Probes for a Bluetooth adapter and updates UI state accordingly."""
        self.log("Checking for Bluetooth adapter...", LogLevel.INF)
        if await self._is_ble_available():
            self.log("Bluetooth adapter ready.", LogLevel.INF)
            self._btn_start_enable()
            self._btn_stop_disable()
            self._btn_check_ble_disable()
            self._btn_disconnect_disable()
        else:
            self.log("Bluetooth adapter was disconnected.", LogLevel.ERR)
            self._update_ui_on_adapter_disappeared()

    async def _async_check_bluetooth_availability_on_stop_scan(self):
        self.log("Checking for Bluetooth adapter...", LogLevel.INF)
        if await self._is_ble_available():
            self._btn_start_enable()
            self._btn_stop_disable()
            self._btn_check_ble_disable()
            self._btn_disconnect_disable()
        else:
            self.log("Bluetooth adapter was disconnected.", LogLevel.ERR)
            self._update_ui_on_adapter_disappeared()

    async def _async_scan(self):
        """Performs a BLE scan, with periodic checks for adapter availability."""
        self.log("Starting scan...", LogLevel.INF)
        if not await self._is_ble_available():
            self.log("Bluetooth adapter not available. Scan cancelled.", LogLevel.ERR)
            self.after(0, self._update_ui_on_disconnect)
            return

        self.discovered_devices.clear()
        self._device_listbox_clear()
        self._btn_stop_enable()
        self.scanning = True

        def detection_callback(device, adv):
            if SERVICE_UUID.lower() in [u.lower() for u in adv.service_uuids]:
                key = f"{device.address} - {device.name or 'Unknown'}"
                if key not in self.discovered_devices:
                    self.log(f"Discovered {device.address} - {device.name}", LogLevel.INF)
                    self.discovered_devices[key] = device.address
                    self.after(0, self.device_listbox.insert, 'end', key)

        scanner = BleakScanner(detection_callback=detection_callback)
        try:
            await scanner.start()
            start_time = time.time()

            # Main scanning loop with periodic health checks
            while self.scanning and (time.time() - start_time) < SCAN_DURATION:
                await asyncio.sleep(1.0)  # Check every second
                if not await self._is_ble_available():
                    self.log("Bluetooth adapter disappeared during scan!", LogLevel.ERR)
                    self.scanning = False  # This will break the loop
                    self.after(0, self._update_ui_on_adapter_disappeared)
                    return  # Exit the coroutine immediately

            await scanner.stop()

        except Exception as e:
            self.log(f"An error occurred during scan: {e}", LogLevel.ERR)
        finally:
            # This block ensures the UI is always reset correctly
            self.scanning = False
            self.after(0, self._scan_finished)

    async def _async_stop_scan_and_connect(self, address: str):
        if self.scanning and self.scan_task and not self.scan_task.done():
            self.log("Scan in progress, awaiting completion...", LogLevel.INF)
            self.scanning = False
            await asyncio.wrap_future(self.scan_task)
            self.log("Scan is confirmed stopped.", LogLevel.DBG)
            self._btn_start_disable()

        self.log(f"Connecting to {address} ...", LogLevel.INF)
        transport = SMPBLETransport()
        self.client = SMPClient(transport, address)
        try:
            await self.client.connect()
            self.log("Connected successfully.", LogLevel.INF)
            self.after(0, self._update_ui_on_connect)

            if self.disconnect_monitor_task and not self.disconnect_monitor_task.done():
                self.disconnect_monitor_task.cancel()
            self.disconnect_monitor_task = self.async_loop.create_task(self._monitor_disconnection())

        except Exception as e:
            self.log(f"Connection failed: {e}", LogLevel.ERR)
            self.client = None
            self.after(0, self._update_ui_on_disconnect)

    async def _async_disconnect(self, manual: bool = True):
        if self.disconnect_monitor_task and not self.disconnect_monitor_task.done():
            self.log("Cancelling disconnection monitor.", LogLevel.DBG)
            self.disconnect_monitor_task.cancel()

        if self.client:
            try:
                if manual:
                    await self.client.disconnect()
            except Exception as e:
                self.log(f"Error during disconnect: {e}", LogLevel.ERR)
            finally:
                self.after(0, self._update_ui_on_disconnect)

    async def _monitor_disconnection(self):
        self.log("Disconnection monitor started.", LogLevel.DBG)
        if not self.client:
            self.log("Client does not exist, stopping monitor.", LogLevel.WRN)
            return

        try:
            transport = self.client._transport
            if isinstance(transport, SMPBLETransport):
                await transport._disconnected_event.wait()
                self.log("Disconnection event detected by monitor!", LogLevel.WRN)
                self.after(0, self._update_ui_on_disconnect)
            else:
                self.log("Transport is not BLE, cannot monitor disconnection event.", LogLevel.WRN)

        except asyncio.CancelledError:
            self.log("Disconnection monitor was cancelled.", LogLevel.DBG)
        except Exception as e:
            self.log(f"Error in disconnection monitor: {e}", LogLevel.ERR)

    async def _async_send_command(self):
        try:
            async with self.command_lock:
                if not self.client:
                    return

                await asyncio.sleep(DEBOUNCE_DELAY_S)

                with self.values_lock:
                    vals = self.latest_led_values[:]

                ctrl_mode = self.control_mode.get()
                brightness_value = None
                if ctrl_mode in ["rgb", "aqi"]:
                    brightness_value = self.brightness_scale.get()
                    # Use dimmed RGB values for PWM channels
                    vals[3] = self.rgb_dimmed['R']
                    vals[4] = self.rgb_dimmed['G']
                    vals[5] = self.rgb_dimmed['B']
                pwm_uncompensated = vals[3:]
                if brightness_value is not None:
                    pwm_r, pwm_g, pwm_b = get_pwm_compensation_for_brightness(brightness_value, vals[3], vals[4], vals[5])
                    vals[3] = pwm_r
                    vals[4] = pwm_g
                    vals[5] = pwm_b

                try:
                    cmd = f"ruuvi led_write_channels {' '.join(map(str, vals))}"
                    if brightness_value is None:
                        self.log(f"Sending: {cmd}", LogLevel.INF)
                    else:
                        self.log(f"Sending: {cmd} (orig PWM: {pwm_uncompensated})", LogLevel.INF)
                    await self.client.request(Execute(argv=cmd.split()))
                    self.log("Command sent successfully.", LogLevel.DBG)
                except Exception as e:
                    self.log(f"Command Error during request: {e}", LogLevel.ERR)
                    self.after(0, self._update_ui_on_disconnect)
        except Exception as e:
            self.log(f"CRITICAL - Unhandled exception in send coroutine: {e}", LogLevel.ERR)

    async def _async_send_manual_brightness_ctrl(self, brightness: str):
        try:
            async with self.command_lock:
                if not self.client:
                    return

                try:
                    cmd = f"ruuvi led_brightness {brightness}"
                    self.log(f"Sending: {cmd}", LogLevel.INF)
                    await self.client.request(Execute(argv=cmd.split()))
                    self.log("Command sent successfully.", LogLevel.DBG)
                except Exception as e:
                    self.log(f"Command Error during request: {e}", LogLevel.ERR)
                    self.after(0, self._update_ui_on_disconnect)
        except Exception as e:
            self.log(f"CRITICAL - Unhandled exception in send coroutine: {e}", LogLevel.ERR)


    # --- UI Update Callbacks ---
    def _scan_finished(self):
        self.log("Scan finished.", LogLevel.INF)
        self._btn_stop_disable()
        asyncio.run_coroutine_threadsafe(self._async_check_bluetooth_availability_on_stop_scan(), self.async_loop)

    def _update_ui_on_connect(self):
        self.log(f"Updating UI for CONNECTED state.", LogLevel.DBG)
        self._update_status_circle(StatusColor.GREEN)
        self._sliders_enable()
        self._btn_disconnect_enable()
        self._btn_connect_disable()
        self._btn_start_disable()
        self._btn_check_ble_disable()
        self._set_led_control_frame_state(True)  # Enable mode selection when connected
        self._update_control_frames_state()

    def _update_ui_on_adapter_disappeared(self):
        self._update_status_circle(StatusColor.GREY)
        self._btn_start_disable()
        self._btn_stop_disable()
        self._btn_check_ble_enable()
        self._device_listbox_clear()
        self._btn_connect_disable()
        self._btn_disconnect_disable()
        self._sliders_disable()
        self._set_led_control_frame_state(False)  # Disable mode selection when adapter is gone

    def _update_ui_on_disconnect(self):
        self.log(f"Disconnected.", LogLevel.INF)
        if self.client is None:
            return

        self.client = None
        self._update_status_circle(StatusColor.RED)
        self._btn_disconnect_disable()
        if self.device_listbox.curselection():
            self._btn_connect_enable()
        else:
            self._btn_connect_disable()
        self._sliders_disable()  # This will disable all control frames
        self._set_led_control_frame_state(False)  # Disable mode selection when disconnected

        if threading.get_ident() == self.main_thread_id:
            self._reset_current_color_canvases_and_info()
        else:
            self.after(0, self._reset_current_color_canvases_and_info)

        asyncio.run_coroutine_threadsafe(self._async_check_bluetooth_availability_on_disconnect(), self.async_loop)


if __name__ == '__main__':
    app = BLELEDControllerApp()
    app.mainloop()
    
