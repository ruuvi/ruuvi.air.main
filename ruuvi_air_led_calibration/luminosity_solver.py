#!/usr/bin/env python3
"""
luminocity_solver.py
Simplified diagonal-only white solver using only the wide (L) channel.

- Uses measured R_L, G_L, B_L vs Current (other columns are ignored)
- Defines 100% per-channel target as min(last R_L, last G_L, last B_L)
- For each brightness percent P:
    * L_target = (P/100) * L_limit
    * For each LED i ∈ {R,G,B}:
        - find precise current c_i via linear interpolation so L_i(c_i) = L_target
        - set integer current d_i = ceil(c_i)
        - compute PWM dim (0..255):
            • luminance mode: duty = L_target / L_i(d_i), dim_i = round(255 * duty)
            • current   mode: duty = c_i / d_i (if d_i>0 else 0), dim_i = round(255 * duty)
- Adds an implicit (0,0) point to each LED curve for robust low-end interpolation.
- Enforces non-decreasing luminance to avoid tiny noise glitches.

Outputs CSV:
  Percent,d_R,d_G,d_B,dim_R,dim_G,dim_B,c_R,c_G,c_B
"""

import sys

# ---- Minimum Python version check (requires PEP 604 union types) ----
_MIN_PY = (3, 10)
if sys.version_info < _MIN_PY:
    sys.stderr.write(
        f"[ERROR] Python {_MIN_PY[0]}.{_MIN_PY[1]}+ is required; "
        f"you are on {sys.version_info.major}.{sys.version_info.minor}\n"
    )
    sys.exit(1)

import argparse
from typing import Dict, List, Sequence, Tuple

import numpy as np
import pandas as pd


# ---------------- Utilities ----------------

def log_error(msg: str) -> None:
    print(f"[ERROR] {msg}", file=sys.stderr)


def log_info(msg: str) -> None:
    print(f"[INFO]  {msg}", file=sys.stderr)


def require_columns_case_insensitive(
    df: pd.DataFrame, names: Sequence[str]
) -> List[str]:
    """
    Return actual column names (original casing) for the requested logical names
    (case-insensitive). Raise if any are missing.
    """
    lower_map: Dict[str, str] = {c.lower(): c for c in df.columns}
    resolved: List[str] = []
    missing: List[str] = []
    for n in names:
        key = n.lower()
        if key in lower_map:
            resolved.append(lower_map[key])
        else:
            missing.append(n)
    if missing:
        raise ValueError(f"Missing required columns: {missing}. Found: {list(df.columns)}")
    return resolved


def enforce_non_decreasing(y: np.ndarray) -> np.ndarray:
    """Make array non-decreasing (guards against small measurement noise)."""
    y = np.asarray(y, dtype=float).copy()
    return np.maximum.accumulate(y)


def add_origin_anchor(x: np.ndarray, y: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
    """
    Ensure there is an explicit (0,0) point for robust interpolation near black.
    """
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    if x.size == 0:
        return np.array([0.0], dtype=float), np.array([0.0], dtype=float)
    if x[0] > 0.0 or y[0] > 0.0:
        x = np.insert(x, 0, 0.0)
        y = np.insert(y, 0, 0.0)
    return x, y


def invert_piecewise_linear(
    x: np.ndarray, y: np.ndarray, target: float
) -> float:
    """
    Invert monotone piecewise-linear y(x) for x such that y(x) = target.
    Assumes x is strictly increasing and y is non-decreasing.
    Includes endpoints; clamps to [x[0], x[-1]].
    """
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    if target <= 0.0:
        return float(x[0])
    if target >= float(y[-1]):
        return float(x[-1])

    # Find first segment with y[i-1] <= target <= y[i]
    for i in range(1, len(x)):
        y0, y1 = float(y[i - 1]), float(y[i])
        if target <= y1 + 1e-15:
            x0, x1 = float(x[i - 1]), float(x[i])
            if y1 <= y0 + 1e-12:
                # Flat segment: return the lower x
                return x0
            t = (target - y0) / (y1 - y0)
            return x0 + t * (x1 - x0)

    return float(x[-1])  # Fallback


def evaluate_piecewise_linear(
    x: np.ndarray, y: np.ndarray, x_value: float
) -> float:
    """
    Evaluate y(x) at a scalar x_value using piecewise-linear interpolation.
    """
    xv = float(x_value)
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)

    if xv <= float(x[0]):
        return float(y[0])
    if xv >= float(x[-1]):
        return float(y[-1])

    idx = int(np.searchsorted(x, xv) - 1)
    x0, x1 = float(x[idx]), float(x[idx + 1])
    y0, y1 = float(y[idx]), float(y[idx + 1])
    if x1 <= x0 + 1e-12:
        return y0
    t = (xv - x0) / (x1 - x0)
    return y0 + t * (y1 - y0)


# ---------------- Core loading & LUT building ----------------

def load_l_curves(csv_path: str) -> Tuple[
    Tuple[np.ndarray, np.ndarray],  # (x_R, R_L(x))
    Tuple[np.ndarray, np.ndarray],  # (x_G, G_L(x))
    Tuple[np.ndarray, np.ndarray],  # (x_B, B_L(x))
    float                            # L_limit
]:
    """
    Load CSV and build three (current, luminance) curves for R/G/B L-channels.
    Returns per-channel (x, y) and the brightness limit L_limit defined as
    min(last R_L, last G_L, last B_L).
    """
    df = pd.read_csv(csv_path)
    current_col, r_l_col, g_l_col, b_l_col = require_columns_case_insensitive(
        df, ["Current", "R_L", "G_L", "B_L"]
    )

    # Prepare data
    sub = df[[current_col, r_l_col, g_l_col, b_l_col]].copy()
    for col in [current_col, r_l_col, g_l_col, b_l_col]:
        sub[col] = pd.to_numeric(sub[col], errors="coerce")
    sub = sub.dropna().sort_values(current_col).reset_index(drop=True)

    current = sub[current_col].to_numpy(dtype=float)
    r_l = enforce_non_decreasing(sub[r_l_col].to_numpy(dtype=float))
    g_l = enforce_non_decreasing(sub[g_l_col].to_numpy(dtype=float))
    b_l = enforce_non_decreasing(sub[b_l_col].to_numpy(dtype=float))

    x_r, r_l = add_origin_anchor(current, r_l)
    x_g, g_l = add_origin_anchor(current, g_l)
    x_b, b_l = add_origin_anchor(current, b_l)

    # Convert to Python floats before min() to avoid IDE warnings
    last_r: float = float(r_l[-1])
    last_g: float = float(g_l[-1])
    last_b: float = float(b_l[-1])
    l_limit: float = min(last_r, last_g, last_b)

    log_info(
        f"Last-row luminance: R_L={last_r:.3f}, G_L={last_g:.3f}, B_L={last_b:.3f} → L_limit={l_limit:.3f}"
    )

    return (x_r, r_l), (x_g, g_l), (x_b, b_l), l_limit


def build_lookup_table(
    csv_path: str,
    steps: int,
    out_csv: str,
    pwm_mode: str = "luminance",  # "luminance" or "current"
) -> None:
    """
    Build the LUT using luminance-only diagonal balancing with PWM correction.
    Writes a CSV with columns:
      Percent,d_R,d_G,d_B,dim_R,dim_G,dim_B,c_R,c_G,c_B
    """
    (x_r, r_l), (x_g, g_l), (x_b, b_l), l_limit = load_l_curves(csv_path)

    rows: List[Dict[str, float | int]] = []
    for k in range(steps):
        percent: float = 100.0 * k / (steps - 1 if steps > 1 else 1.0)

        if percent <= 0.0:
            d_r = d_g = d_b = 0
            dim_r = dim_g = dim_b = 0
            c_r = c_g = c_b = 0.0
        else:
            l_target: float = (percent / 100.0) * l_limit

            # Precise currents by inversion on each LED’s L curve: L_i(c_i) = l_target
            c_r: float = invert_piecewise_linear(x_r, r_l, l_target)
            c_g: float = invert_piecewise_linear(x_g, g_l, l_target)
            c_b: float = invert_piecewise_linear(x_b, b_l, l_target)

            # Integer currents (round up)
            d_r: int = int(np.ceil(c_r))
            d_g: int = int(np.ceil(c_g))
            d_b: int = int(np.ceil(c_b))

            if pwm_mode == "luminance":
                # Actual luminance at rounded-up currents
                l_r_up: float = evaluate_piecewise_linear(x_r, r_l, d_r)
                l_g_up: float = evaluate_piecewise_linear(x_g, g_l, d_g)
                l_b_up: float = evaluate_piecewise_linear(x_b, b_l, d_b)

                # Duty needed to hit target (clamped to [0,1])
                duty_r: float = 0.0 if l_r_up <= 0.0 else max(0.0, min(1.0, l_target / l_r_up))
                duty_g: float = 0.0 if l_g_up <= 0.0 else max(0.0, min(1.0, l_target / l_g_up))
                duty_b: float = 0.0 if l_b_up <= 0.0 else max(0.0, min(1.0, l_target / l_b_up))
            elif pwm_mode == "current":
                duty_r = 0.0 if d_r <= 0 else max(0.0, min(1.0, c_r / d_r))
                duty_g = 0.0 if d_g <= 0 else max(0.0, min(1.0, c_g / d_g))
                duty_b = 0.0 if d_b <= 0 else max(0.0, min(1.0, c_b / d_b))
            else:
                raise ValueError(f"Unsupported pwm_mode: {pwm_mode}")

            # PWM in 0..255 (round to nearest)
            dim_r: int = int(np.clip(np.rint(255.0 * duty_r), 0, 255))
            dim_g: int = int(np.clip(np.rint(255.0 * duty_g), 0, 255))
            dim_b: int = int(np.clip(np.rint(255.0 * duty_b), 0, 255))

        rows.append(
            {
                "Percent": int(round(percent)),
                "d_R": d_r,
                "d_G": d_g,
                "d_B": d_b,
                "dim_R": dim_r,
                "dim_G": dim_g,
                "dim_B": dim_b,
                "c_R": round(float(c_r), 4),
                "c_G": round(float(c_g), 4),
                "c_B": round(float(c_b), 4),
            }
        )

    out_df = pd.DataFrame(rows)
    out_df.to_csv(out_csv, index=False)
    log_info(f"Wrote LUT: {out_csv}")


# ---------------- CLI ----------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Simplified L-channel white solver → currents + PWM."
    )
    parser.add_argument(
        "input_csv",
        help="Calibration CSV with at least: Current,R_L,G_L,B_L (other columns ignored)",
    )
    parser.add_argument(
        "output_csv",
        help="Output CSV path (Percent,d_R,d_G,d_B,dim_R,dim_G,dim_B,c_R,c_G,c_B)",
    )
    parser.add_argument(
        "--steps", type=int, default=101, help="Number of brightness steps (default 101 → 0..100%)"
    )
    parser.add_argument(
        "--pwm-mode", "--pwm_mode",
        choices=["luminance", "current"],
        default="luminance",
        help="PWM calculation: 'luminance' uses L_target/L(ceil(c)); 'current' uses c/ceil(c). Default: luminance.",
    )
    args = parser.parse_args()

    try:
        build_lookup_table(
            csv_path=args.input_csv,
            steps=args.steps,
            out_csv=args.output_csv,
            pwm_mode=args.pwm_mode,
        )
    except Exception as e:
        log_error(str(e))
        sys.exit(1)


if __name__ == "__main__":
    main()
