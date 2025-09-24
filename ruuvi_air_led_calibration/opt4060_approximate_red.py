#!/usr/bin/env python3

import argparse
import csv
import sys


def err(msg):
    print(f"[ERROR] {msg}", file=sys.stderr)


def info(msg):
    print(f"[INFO] {msg}", file=sys.stderr)


def load_rows(fin):
    """Load CSV data into a list of dictionaries with proper column mapping."""
    rdr = csv.reader(fin)
    try:
        header = next(rdr)
    except StopIteration:
        raise ValueError("Input CSV is empty.")
    except Exception as e:
        raise ValueError(f"Failed to read CSV header: {e}")

    # Expected columns
    expected = ["current", "r_r", "r_g", "r_b", "r_l", "g_r", "g_g", "g_b", "g_l", "b_r", "b_g", "b_b", "b_l"]

    # Create case-insensitive mapping
    idx = {name.strip().lower(): i for i, name in enumerate(header)}
    missing = [c for c in expected if c not in idx]
    if missing:
        raise ValueError(f"Missing required columns: {', '.join(missing)}")

    # Get column indices
    col_indices = {col: idx[col] for col in expected}
    max_index = max(col_indices.values())

    rows = []
    line_no = 1  # header is line 1
    for row in rdr:
        line_no += 1
        if len(row) <= max_index:
            err(f"Line {line_no}: not enough columns (have {len(row)}, need at least {max_index + 1}). Row skipped.")
            continue

        try:
            # Parse all numeric columns
            parsed_row = {
                "line_no": line_no,
                "current": int(row[col_indices["current"]].strip()),
                "r_r": float(row[col_indices["r_r"]].strip()),
                "r_g": float(row[col_indices["r_g"]].strip()),
                "r_b": float(row[col_indices["r_b"]].strip()),
                "r_l": float(row[col_indices["r_l"]].strip()),
                "g_r": float(row[col_indices["g_r"]].strip()),
                "g_g": float(row[col_indices["g_g"]].strip()),
                "g_b": float(row[col_indices["g_b"]].strip()),
                "g_l": float(row[col_indices["g_l"]].strip()),
                "b_r": float(row[col_indices["b_r"]].strip()),
                "b_g": float(row[col_indices["b_g"]].strip()),
                "b_b": float(row[col_indices["b_b"]].strip()),
                "b_l": float(row[col_indices["b_l"]].strip()),
            }
            rows.append(parsed_row)
        except Exception as e:
            err(f"Line {line_no}: failed to parse numeric values: {e}. Row skipped.")
            continue

    return rows


def find_red_calibration_point(rows, r_threshold=300.0):
    """
    Find the first row where R_R > threshold to use as calibration point.
    Returns (index, ratios) where ratios = (r_r/current, r_g/current, r_b/current, r_l/current)
    """
    for i, row in enumerate(rows):
        if row["r_r"] > r_threshold:
            current = row["current"]
            if current <= 0:
                err(f"Row {i + 2} (line ~{row['line_no']}): Found R_R={row['r_r']:.1f} > {r_threshold}, but current is {current}. Cannot calculate ratios.")
                continue

            ratios = (
                row["r_r"] / current,
                row["r_g"] / current,
                row["r_b"] / current,
                row["r_l"] / current
            )

            info(f"Found Red calibration point at row {i + 2} (line ~{row['line_no']}): "
                 f"Current={current}, R_R={row['r_r']:.1f}, ratios=({ratios[0]:.2f}, {ratios[1]:.2f}, {ratios[2]:.2f}, {ratios[3]:.6f})")

            return i, ratios

    err(f"Could not find Red calibration point. No row found where R_R > {r_threshold}.")
    return None, None


def approximate_red_values(rows, calibration_index, ratios):
    """
    Approximate R_R, R_G, R_B, R_L values for rows before calibration point.
    """
    r_r_ratio, r_g_ratio, r_b_ratio, r_l_ratio = ratios
    approximated_count = 0

    for i in range(calibration_index):
        row = rows[i]
        current = row["current"]

        # Calculate approximated values
        approx_r_r = current * r_r_ratio
        approx_r_g = current * r_g_ratio
        approx_r_b = current * r_b_ratio
        approx_r_l = current * r_l_ratio

        # Store original values for logging
        orig_values = (row["r_r"], row["r_g"], row["r_b"], row["r_l"])

        # Update the row with approximated values
        row["r_r"] = approx_r_r
        row["r_g"] = approx_r_g
        row["r_b"] = approx_r_b
        row["r_l"] = approx_r_l

        approximated_count += 1

        # Log the approximation (only for first few rows to avoid spam)
        if i < 3:
            info(f"Row {i + 2} (line ~{row['line_no']}): Current={current}, "
                 f"Original R=({orig_values[0]:.1f}, {orig_values[1]:.1f}, {orig_values[2]:.1f}, {orig_values[3]:.6f}), "
                 f"Approximated R=({approx_r_r:.1f}, {approx_r_g:.1f}, {approx_r_b:.1f}, {approx_r_l:.6f})")

    info(f"Approximated Red values for {approximated_count} rows before calibration point.")
    return approximated_count


def write_rows(fout, rows):
    """Write rows to output CSV file."""
    writer = csv.writer(fout)

    # Write header
    writer.writerow(["Current", "R_R", "R_G", "R_B", "R_L", "G_R", "G_G", "G_B", "G_L", "B_R", "B_G", "B_B", "B_L"])
    writer.writerow([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])

    # Write data rows
    for row in rows:
        writer.writerow([
            row["current"],  # Integer, no formatting needed
            f"{row['r_r']:.3f}", f"{row['r_g']:.3f}", f"{row['r_b']:.3f}", f"{row['r_l']:.3f}",
            f"{row['g_r']:.3f}", f"{row['g_g']:.3f}", f"{row['g_b']:.3f}", f"{row['g_l']:.3f}",
            f"{row['b_r']:.3f}", f"{row['b_g']:.3f}", f"{row['b_b']:.3f}", f"{row['b_l']:.3f}"
        ])


def main():
    ap = argparse.ArgumentParser(
        description=("Approximate Red LED values for low currents where the Red LED doesn't work properly. "
                     "Finds the first row where R_R > threshold, calculates ratios (R_*/Current), "
                     "then approximates missing Red values for earlier rows using these ratios."))
    ap.add_argument("input",
                    help="Input CSV file with columns: Current,R_R,R_G,R_B,R_L,G_R,G_G,G_B,G_L,B_R,B_G,B_B,B_L")
    ap.add_argument("output", help="Output CSV file (same format as input)")
    ap.add_argument("--r-threshold", type=float, default=300.0,
                    help="R_R threshold for finding calibration point. Default: 300.0")
    args = ap.parse_args()

    # Open files
    try:
        fin = open(args.input, "r", newline="", encoding="utf-8")
    except Exception as e:
        err(f"Cannot open input file '{args.input}': {e}")
        sys.exit(1)

    try:
        fout = open(args.output, "w", newline="", encoding="utf-8")
    except Exception as e:
        fin.close()
        err(f"Cannot open output file '{args.output}': {e}")
        sys.exit(1)

    with fin, fout:
        try:
            rows = load_rows(fin)
        except ValueError as e:
            err(str(e))
            sys.exit(1)

        if not rows:
            err("No valid data rows found.")
            sys.exit(1)

        # Find Red calibration point
        calibration_index, ratios = find_red_calibration_point(rows, args.r_threshold)
        if calibration_index is None:
            sys.exit(1)

        # Approximate Red values for rows before calibration point
        if calibration_index > 0:
            approximated_count = approximate_red_values(rows, calibration_index, ratios)
            info(
                f"Processing complete. Approximated {approximated_count} rows, kept {len(rows) - approximated_count} original rows.")
        else:
            info("No rows need approximation - calibration point is at the first row.")

        # Write output
        write_rows(fout, rows)
        info(f"Output written to '{args.output}' with {len(rows)} total rows.")


if __name__ == "__main__":
    main()
