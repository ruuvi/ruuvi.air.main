#!/usr/bin/env python3
"""
emit_led_calibration_c.py

Read a luminocity_solver output CSV and emit C header/source with six uint8_t arrays:
- g_led_calibration_brightness_to_current_red
- g_led_calibration_brightness_to_current_green
- g_led_calibration_brightness_to_current_blue
- g_led_calibration_brightness_to_pwm_red
- g_led_calibration_brightness_to_pwm_green
- g_led_calibration_brightness_to_pwm_blue

Usage:
  python emit_led_calibration_c.py out.csv out/led_cal

Produces:
  out/led_cal.h
  out/led_cal.c
"""

import sys
_MIN_PY = (3, 10)
if sys.version_info < _MIN_PY:
    sys.stderr.write(
        f"[ERROR] Python {_MIN_PY[0]}.{_MIN_PY[1]}+ is required; "
        f"you are on {sys.version_info.major}.{sys.version_info.minor}\n"
    )
    sys.exit(1)

import argparse
import os
import re
from typing import Dict, List, Sequence, Tuple

import numpy as np
import pandas as pd


# ---------- helpers ----------

def require_columns_case_insensitive(df: pd.DataFrame, names: Sequence[str]) -> Dict[str, str]:
    """
    Return a mapping from requested logical names (case-insensitive) to actual column names.
    Raises if any are missing.
    """
    lower_map: Dict[str, str] = {c.lower(): c for c in df.columns}
    resolved: Dict[str, str] = {}
    missing: List[str] = []
    for n in names:
        key = n.lower()
        if key in lower_map:
            resolved[n] = lower_map[key]
        else:
            missing.append(n)
    if missing:
        raise ValueError(f"Missing required columns: {missing}. Found: {list(df.columns)}")
    return resolved


def sanitize_macro_base(base: str) -> str:
    """
    Build a header-guard macro from a filename base.
    """
    s = re.sub(r'[^A-Za-z0-9_]', '_', base)
    if not re.match(r'[A-Za-z_]', s):
        s = "_" + s
    return s.upper()


def chunk_lines(values: List[int], per_line: int = 16) -> str:
    lines: List[str] = []
    for i in range(0, len(values), per_line):
        lines.append(", ".join(str(int(v)) for v in values[i:i+per_line]))
    return ",\n".join(lines)


# ---------- core ----------

def load_arrays_from_csv(csv_path: str) -> Tuple[List[int], List[int], List[int], List[int], List[int], List[int]]:
    df = pd.read_csv(csv_path)

    cols = require_columns_case_insensitive(
        df, ["d_R", "d_G", "d_B", "dim_R", "dim_G", "dim_B"]
    )

    d_r = np.clip(np.rint(pd.to_numeric(df[cols["d_R"]], errors="coerce")), 0, 255).astype(int).tolist()
    d_g = np.clip(np.rint(pd.to_numeric(df[cols["d_G"]], errors="coerce")), 0, 255).astype(int).tolist()
    d_b = np.clip(np.rint(pd.to_numeric(df[cols["d_B"]], errors="coerce")), 0, 255).astype(int).tolist()

    dim_r = np.clip(np.rint(pd.to_numeric(df[cols["dim_R"]], errors="coerce")), 0, 255).astype(int).tolist()
    dim_g = np.clip(np.rint(pd.to_numeric(df[cols["dim_G"]], errors="coerce")), 0, 255).astype(int).tolist()
    dim_b = np.clip(np.rint(pd.to_numeric(df[cols["dim_B"]], errors="coerce")), 0, 255).astype(int).tolist()

    n = {len(d_r), len(d_g), len(d_b), len(dim_r), len(dim_g), len(dim_b)}
    if len(n) != 1:
        raise ValueError("All arrays must have the same length.")

    return d_r, d_g, d_b, dim_r, dim_g, dim_b


def emit_header(
    h_path: str,
    macro_base: str,
    steps: int,
) -> None:
    guard = f"{macro_base}_H"
    header = f"""/* Auto-generated from CSV; do not edit by hand. */
#ifndef {guard}
#define {guard}

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define LED_CALIBRATION_BRIGHTNESS_STEPS ({steps}u)

/* Brightness → current (LP5810 0..255) */
extern const uint8_t g_led_calibration_brightness_to_current_red  [LED_CALIBRATION_BRIGHTNESS_STEPS];
extern const uint8_t g_led_calibration_brightness_to_current_green[LED_CALIBRATION_BRIGHTNESS_STEPS];
extern const uint8_t g_led_calibration_brightness_to_current_blue [LED_CALIBRATION_BRIGHTNESS_STEPS];

/* Brightness → PWM dim (0..255) */
extern const uint8_t g_led_calibration_brightness_to_pwm_red      [LED_CALIBRATION_BRIGHTNESS_STEPS];
extern const uint8_t g_led_calibration_brightness_to_pwm_green    [LED_CALIBRATION_BRIGHTNESS_STEPS];
extern const uint8_t g_led_calibration_brightness_to_pwm_blue     [LED_CALIBRATION_BRIGHTNESS_STEPS];

#ifdef __cplusplus
}} /* extern "C" */
#endif

#endif /* {guard} */
"""
    os.makedirs(os.path.dirname(h_path), exist_ok=True) if os.path.dirname(h_path) else None
    with open(h_path, "w", encoding="utf-8") as f:
        f.write(header)


def emit_source(
    c_path: str,
    header_basename: str,
    steps: int,
    d_r: List[int], d_g: List[int], d_b: List[int],
    dim_r: List[int], dim_g: List[int], dim_b: List[int],
    per_line: int = 16,
) -> None:
    body = f"""/* Auto-generated from CSV; do not edit by hand. */
#include "{header_basename}"

const uint8_t g_led_calibration_brightness_to_current_red  [LED_CALIBRATION_BRIGHTNESS_STEPS] = {{
{chunk_lines(d_r, per_line)}
}};

const uint8_t g_led_calibration_brightness_to_current_green[LED_CALIBRATION_BRIGHTNESS_STEPS] = {{
{chunk_lines(d_g, per_line)}
}};

const uint8_t g_led_calibration_brightness_to_current_blue [LED_CALIBRATION_BRIGHTNESS_STEPS] = {{
{chunk_lines(d_b, per_line)}
}};

const uint8_t g_led_calibration_brightness_to_pwm_red      [LED_CALIBRATION_BRIGHTNESS_STEPS] = {{
{chunk_lines(dim_r, per_line)}
}};

const uint8_t g_led_calibration_brightness_to_pwm_green    [LED_CALIBRATION_BRIGHTNESS_STEPS] = {{
{chunk_lines(dim_g, per_line)}
}};

const uint8_t g_led_calibration_brightness_to_pwm_blue     [LED_CALIBRATION_BRIGHTNESS_STEPS] = {{
{chunk_lines(dim_b, per_line)}
}};
"""
    os.makedirs(os.path.dirname(c_path), exist_ok=True) if os.path.dirname(c_path) else None
    with open(c_path, "w", encoding="utf-8") as f:
        f.write(body)


def main() -> None:
    ap = argparse.ArgumentParser(description="Emit C header/source with LED calibration LUTs.")
    ap.add_argument("input_csv", help="CSV from luminocity_solver (must have d_R,d_G,d_B,dim_R,dim_G,dim_B)")
    ap.add_argument("out_prefix", help="Output prefix (e.g., out/led_cal) → writes .h and .c")
    ap.add_argument("--per-line", type=int, default=16, help="Values per line in C arrays")
    args = ap.parse_args()

    d_r, d_g, d_b, dim_r, dim_g, dim_b = load_arrays_from_csv(args.input_csv)
    steps = len(d_r)

    basefile = os.path.basename(args.out_prefix)
    macro_base = sanitize_macro_base(basefile)
    h_path = args.out_prefix + ".h"
    c_path = args.out_prefix + ".c"

    emit_header(h_path, macro_base, steps)
    emit_source(c_path, os.path.basename(h_path), steps, d_r, d_g, d_b, dim_r, dim_g, dim_b, args.per_line)

    print(h_path)
    print(c_path)


if __name__ == "__main__":
    main()

