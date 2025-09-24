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
1,3,1,1,197,71,102,2.2405,0.277,0.4016
2,5,1,1,241,141,205,4.7182,0.5539,0.8031
3,7,1,2,254,212,188,6.9747,0.8309,1.322
4,10,2,2,236,189,250,9.2445,1.2167,1.9537
5,12,2,3,244,236,223,11.5579,1.7732,2.5672
6,14,3,4,251,210,209,13.8006,2.3147,3.1811
7,17,3,4,240,245,244,16.0752,2.846,3.7992
8,19,4,5,245,227,229,18.2629,3.4149,4.4265
9,21,4,6,249,255,218,20.4873,3.9993,5.0577
10,23,5,6,251,241,243,22.6483,4.6274,5.6827
11,25,6,7,254,230,233,24.8621,5.2543,6.318
12,28,6,7,247,251,254,27.071,5.8793,6.964
13,30,7,8,249,239,243,29.3071,6.474,7.6029
14,32,8,9,254,231,235,31.8574,7.0657,8.2376
15,34,8,9,255,247,251,33.9556,7.6939,8.8657
16,37,9,10,249,240,245,36.1326,8.3403,9.5343
17,39,10,11,251,235,239,38.3018,9.0043,10.2139
18,41,10,11,252,249,253,40.58,9.6881,10.8935
19,43,11,12,254,243,247,42.7983,10.3759,11.5797
20,46,12,13,251,239,242,45.01,11.0684,12.2641
21,48,12,13,252,251,254,47.2856,11.7727,12.9437
22,50,13,14,252,247,249,49.5118,12.4825,13.6455
23,52,14,15,254,244,245,51.8538,13.2062,14.3538
24,55,14,16,250,254,242,54.0331,13.9602,15.0695
25,57,15,16,252,252,252,56.264,14.7634,15.8135
26,59,16,17,253,248,248,58.6091,15.4945,16.5064
27,61,17,18,254,246,246,60.8169,16.2193,17.2102
28,64,18,18,252,243,255,63.0772,17.0082,17.9893
29,66,18,19,253,252,251,65.3515,17.7386,18.6781
30,68,19,20,253,250,249,67.5602,18.5176,19.4061
31,70,20,21,255,248,246,69.9805,19.3108,20.1584
32,73,21,21,252,246,254,72.0582,20.088,20.8707
33,75,21,22,253,254,251,74.494,20.8882,21.6338
34,77,22,23,254,252,249,76.7947,21.7182,22.3974
35,80,23,24,252,251,247,79.1047,22.523,23.1533
36,82,24,24,253,249,254,81.3209,23.3283,23.9171
37,84,25,25,254,248,252,83.792,24.1574,24.6637
38,86,25,26,255,255,250,85.9851,24.986,25.4419
39,89,26,27,254,254,248,88.5277,25.8325,26.226
40,91,27,27,255,252,255,90.9514,26.656,26.9607
41,94,28,28,254,251,254,93.4747,27.4869,27.8578
42,97,29,29,252,251,252,96.0254,28.3584,28.6523
43,99,30,30,253,252,250,98.2117,29.3371,29.41
44,101,31,31,254,250,251,100.5879,30.311,30.2309
45,103,32,32,255,250,249,102.9493,31.1076,31.2272
46,106,33,32,253,249,255,105.3962,32.1104,31.9625
47,108,33,33,255,254,253,107.9483,32.9164,32.7698
48,111,34,34,254,254,253,110.3717,33.8249,33.62
49,113,35,35,254,254,251,112.6324,34.7693,34.4461
50,116,36,36,253,253,251,115.0658,35.7139,35.2552
51,118,37,37,254,253,250,117.5819,36.6458,36.1071
52,121,38,38,254,253,250,120.0603,37.5967,37.0383
53,123,39,38,254,253,254,122.5979,38.5404,37.8915
54,126,40,39,253,253,253,125.2157,39.5219,38.7233
55,128,41,40,254,253,253,127.3376,40.5283,39.5519
56,130,42,41,255,253,252,129.9309,41.5102,40.4037
57,133,43,42,254,253,251,132.5342,42.4922,41.2705
58,136,44,43,253,253,251,135.0407,43.4665,42.1186
59,138,45,44,254,253,251,137.258,44.4511,43.0193
60,140,46,45,255,252,250,139.8721,45.4291,44.0016
61,143,47,45,254,253,255,142.4923,46.4948,44.8934
62,146,48,46,253,253,254,145.1098,47.5686,45.7622
63,148,49,47,255,253,254,147.7671,48.5274,46.6622
64,151,50,48,254,254,253,150.2642,49.6281,47.6135
65,153,51,49,255,254,253,152.8271,50.7548,48.5111
66,156,52,50,254,254,253,155.5548,51.7474,49.4121
67,159,53,51,254,254,253,158.4055,52.806,50.386
68,162,54,52,253,255,252,161.0186,53.8906,51.3222
69,164,56,53,254,251,252,163.2983,55.0005,52.1999
70,167,56,54,254,255,252,166.1942,55.9059,53.1159
71,169,57,55,255,255,252,168.6938,56.979,54.1446
72,172,59,56,255,252,252,171.7832,58.0389,55.0468
73,175,60,57,254,253,251,174.3226,59.2613,56.0123
74,178,61,57,254,253,255,177.232,60.3673,56.9778
75,180,62,59,255,254,251,179.7951,61.5213,58.0016
76,183,63,59,254,254,254,182.6625,62.6455,58.8248
77,186,64,60,254,254,255,185.29,63.7546,59.8675
78,189,66,61,254,252,254,188.1773,65.1078,60.8165
79,191,67,62,255,253,254,190.9623,66.1084,61.8079
80,194,68,63,255,253,254,193.673,67.2694,62.7335
81,197,69,64,254,254,254,196.2973,68.5128,63.7045
82,200,70,65,254,254,254,199.4206,69.6839,64.6432
83,203,71,66,254,255,254,202.1237,70.8441,65.6415
84,206,73,67,255,253,254,205.6914,72.0118,66.7134
85,209,74,68,254,253,254,208.0424,73.2854,67.7043
86,211,75,69,255,253,254,210.782,74.3685,68.5985
87,214,76,70,255,254,254,213.7844,75.59,69.6597
88,218,77,71,254,255,254,217.0337,76.8202,70.7163
89,221,79,72,254,253,254,220.1144,78.1164,71.7581
90,224,80,73,254,254,254,223.0071,79.3362,72.792
91,226,81,74,255,254,254,225.9565,80.6568,73.7574
92,230,82,75,254,255,255,229.001,81.8612,74.9886
93,233,84,76,255,253,255,232.5768,83.1734,75.871
94,236,85,77,254,254,255,235.3111,84.4022,76.9879
95,239,86,79,255,254,253,238.489,85.7256,78.1379
96,242,87,80,254,255,253,241.297,86.9139,79.2076
97,245,89,81,254,254,253,244.4693,88.3129,80.1658
98,249,90,82,254,254,253,248.176,89.5987,81.1853
99,252,91,83,254,255,254,251.1135,90.8944,82.3488
100,255,93,84,255,253,254,255.0,92.198,83.491
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

        # Initially disable mode selection since we're not connected
        self._set_mode_radiobuttons_state(False)

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
                ttk.Label(cf, text=label_text).grid(row=jdx, column=0, padx=5, pady=2, sticky='w')
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
            ttk.Label(frame, text=label_text).grid(row=idx, column=0, padx=5, pady=2, sticky='w')
            scale = ttk.Scale(frame, from_=0, to=255, orient='horizontal',
                              command=lambda v, k=key: self._on_slider_change_rgb(k, v))
            scale.grid(row=idx, column=1, padx=5, pady=2, sticky='we')
            value_label = ttk.Label(frame, text="0", width=4)
            value_label.grid(row=idx, column=2, padx=5, pady=2)
            self.rgb_sliders[key] = (scale, value_label)

        # Brightness sliders start after RGB sliders (row 3 and 4)
        ttk.Label(frame, text="Brightness: LED current").grid(row=3, column=0, padx=5, pady=2, sticky='w')
        self.brightness_label = ttk.Label(frame, text="{}".format(self.brightness), width=4)
        self.brightness_label.grid(row=3, column=2, padx=5, pady=2)
        self.brightness_scale = ttk.Scale(frame, from_=0, to=100, orient='horizontal',
                                          command=self._on_brightness_slider_change)
        self.brightness_scale.grid(row=3, column=1, padx=5, pady=2, sticky='we')
        self.brightness_scale.set(self.brightness)  # Default to 20%

        # PWM brightness slider
        ttk.Label(frame, text="Brightness: HSV -> PWM").grid(row=4, column=0, padx=5, pady=2, sticky='w')
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
        ttk.Label(frame, text="Brightness:").grid(row=2, column=0, padx=5, pady=2, sticky='w')

        # Create frame for radio buttons in a row
        radio_frame = ttk.Frame(frame)
        radio_frame.grid(row=2, column=1, padx=5, pady=2, sticky='w')

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
                        command=self._on_brightness_preset_change).pack(side='left')

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

    def _set_mode_radiobuttons_state(self, enabled: bool):
        """Enable or disable the mode selection radio buttons."""
        state = 'normal' if enabled else 'disabled'
        for radiobutton in self.mode_radiobuttons:
            radiobutton.configure(state=state)

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

    def _update_control_frames_state(self):
        mode = self.control_mode.get()

        # Only enable controls if we're connected
        is_connected = self.client is not None

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

        aqi_colors = {'Excellent': {'R': 0, 'G': 255, 'B': 90},
                      'Good': {'R': 30, 'G': 255, 'B': 0},
                      'Fair': {'R': 240, 'G': 255, 'B': 0},
                      'Poor': {'R': 255, 'G': 80, 'B': 0},
                      'VeryPoor': {'R': 255, 'G': 0, 'B': 0},
                      }

        if self.aqi_value > 89.5:
            color = aqi_colors['Excellent']
        elif self.aqi_value > 79.5:
            color = aqi_colors['Good']
        elif self.aqi_value > 49.5:
            color = aqi_colors['Fair']
        elif self.aqi_value > 9.5:
            color = aqi_colors['Poor']
        else:
            color = aqi_colors['VeryPoor']

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

    def _on_brightness_preset_change(self):
        """Handle brightness preset radio button changes."""
        preset = self.brightness_preset.get()

        if preset == "Night":
            currents = [12, 2, 10]
        elif preset == "Day":
            currents = [35, 6, 20]
        elif preset == "BrightDay":
            currents = [150, 70, 255]
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

        if self.send_task and not self.send_task.done():
            return
        self.send_task = asyncio.run_coroutine_threadsafe(self._async_send_command(), self.async_loop)

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
                if ctrl_mode in ["rgb"]:
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
                    cmd = f"led write_channels LP5810@58 0 {' '.join(map(str, vals))}"
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
        self._set_mode_radiobuttons_state(True)  # Enable mode selection when connected
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
        self._set_mode_radiobuttons_state(False)  # Disable mode selection when adapter is gone

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
        self._set_mode_radiobuttons_state(False)  # Disable mode selection when disconnected

        if threading.get_ident() == self.main_thread_id:
            self._reset_current_color_canvases_and_info()
        else:
            self.after(0, self._reset_current_color_canvases_and_info)

        asyncio.run_coroutine_threadsafe(self._async_check_bluetooth_availability_on_disconnect(), self.async_loop)


if __name__ == '__main__':
    app = BLELEDControllerApp()
    app.mainloop()
    
