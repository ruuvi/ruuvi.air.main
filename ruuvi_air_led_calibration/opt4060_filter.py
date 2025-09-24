#!/usr/bin/env python3

import argparse
import csv
import sys

SERIES_LEN = 16        # Black(4) + Red(4) + Green(4) + Blue(4)
BLOCK_LEN  = 4         # 4 samples per color block
MID_IN_BLOCK = 1       # 0-based 'second row' inside each 4-sample block: indices 1,5,9,13

def err(msg):
    print(f"[ERROR] {msg}", file=sys.stderr)

def info(msg):
    print(f"[INFO] {msg}", file=sys.stderr)

def is_black_row(r, g, b, lum, thr=13.0):
    # Black if ALL four values are strictly less than 10
    return (r < thr) and (g < thr) and (b < thr) and (lum < thr)

def load_rows(fin):
    rdr = csv.reader(fin)
    try:
        header = next(rdr)
    except StopIteration:
        raise ValueError("Input CSV is empty.")
    except Exception as e:
        raise ValueError(f"Failed to read CSV header: {e}")

    need = ["timestamp", "r", "g", "b", "luminosity"]
    idx = {name.strip().lower(): i for i, name in enumerate(header)}
    missing = [c for c in need if c not in idx]
    if missing:
        raise ValueError(f"Missing required columns: {', '.join(missing)}")

    ts_i = idx["timestamp"]; r_i = idx["r"]; g_i = idx["g"]; b_i = idx["b"]; l_i = idx["luminosity"]
    need_max = max(ts_i, r_i, g_i, b_i, l_i)

    rows = []
    line_no = 1  # header is line 1
    for row in rdr:
        line_no += 1
        if len(row) <= need_max:
            err(f"Line {line_no}: not enough columns (have {len(row)}, need at least {need_max+1}). Row skipped.")
            continue
        try:
            r = float(row[r_i].strip())
            g = float(row[g_i].strip())
            b = float(row[b_i].strip())
            lum = float(row[l_i].strip())
        except Exception as e:
            err(f"Line {line_no}: failed to parse numeric values: {e}. Row skipped.")
            continue
        rows.append({
            "line_no": line_no,
            "ts": row[ts_i],
            "r": r, "g": g, "b": b, "lum": lum
        })
    return rows

def write_rgb(writer, rec, current, which):  # which in {"R","G","B"}
    cur_r = current if which == "R" else 0
    cur_g = current if which == "G" else 0
    cur_b = current if which == "B" else 0
    writer.writerow([rec["ts"], cur_r, cur_g, cur_b, rec["r"], rec["g"], rec["b"], rec["lum"]])


def find_data_start(rows, g_threshold=500):
    """
    Find the start of actual data using the algorithm:
    1. Find first row where G > threshold and G > R
    2. Step back by 4 rows to get the Red measurement for current=1

    Returns the index where data processing should start, or None if not found.
    """
    for i, row in enumerate(rows):
        if row["g"] > g_threshold and row["g"] > row["r"]:
            # Found the target row, step back by 4 to get Red measurement
            start_index = i - 4
            if start_index >= 0:
                info(f"Auto-detected data start at row {start_index + 2} (line ~{rows[start_index]['line_no']}), "
                     f"triggered by G={row['g']:.1f} > {g_threshold} at row {i + 2} (line ~{row['line_no']})")
                return start_index
            else:
                err(f"Found trigger at row {i + 2} (line ~{row['line_no']}), but cannot step back 4 rows. "
                    f"Need at least 4 rows before the trigger.")
                return None

    err(f"Could not find data start. No row found where G > {g_threshold} and G > R.")
    return None


def main():
    ap = argparse.ArgumentParser(
        description=("Sync OPT4060 CSV using Black detection. "
                     "Auto-detects data start by finding first row where G > threshold and G > R, "
                     "then steps back 4 rows to find the Red measurement for current=1. "
                     "Then processes subsequent series: on each Black, take rows at +2,+6,+10,+14 "
                     "(midpoints of Black/Red/Green/Blue blocks), build arrays (R,G,B,Black), "
                     "drop Black, and increment current."))
    ap.add_argument("input", help="Input CSV (columns: timestamp,R,G,B,luminosity)")
    ap.add_argument("output", help="Output CSV")
    ap.add_argument("--max-current", type=int, default=255, help="Stop after this current. Default: 255")
    ap.add_argument("--g-threshold", type=float, default=500.0,
                    help="Green threshold for auto-detecting data start. Default: 500.0")
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
            err(str(e)); sys.exit(1)

        if not rows:
            err("No valid data rows found."); sys.exit(1)

        # Auto-detect data start
        data_start = find_data_start(rows, args.g_threshold)
        if data_start is None:
            sys.exit(1)

        writer = csv.writer(fout)
        writer.writerow(["timestamp", "Current_R", "Current_G", "Current_B", "R", "G", "B", "luminosity"])

        N = len(rows)
        i = data_start
        current = 1
        written = 0

        # --- Seed first series from the very first row (user guarantees it's Red midpoint) ---
        if current <= args.max_current:
            if N < 1 + 2 * BLOCK_LEN:
                err("File too short to seed first series (need at least 9 rows). Stopping.")
                sys.exit(1)
            try:
                # R midpoint at i
                write_rgb(writer, rows[i], current, "R")
                # G midpoint at i + 4
                if i + BLOCK_LEN >= N:
                    raise IndexError("Missing Green midpoint (i+4).")
                write_rgb(writer, rows[i + BLOCK_LEN], current, "G")
                # B midpoint at i + 8
                if i + 2 * BLOCK_LEN >= N:
                    raise IndexError("Missing Blue midpoint (i+8).")
                write_rgb(writer, rows[i + 2 * BLOCK_LEN], current, "B")
                written += 3
            except Exception as e:
                err(f"Failed to write seeded first series around line ~{rows[i]['line_no']}: {e}")
                sys.exit(1)
            current += 1
            # After Blue is extracted, start searching for Black
            i = i + 2 * BLOCK_LEN + 1  # continue scanning after the seeded Blue sample

        # --- Process subsequent series: detect Black, then pick every 3rd row in the next 16 ---
        while i < N and current <= args.max_current:
            # Seek next Black
            while i < N and not is_black_row(rows[i]["r"], rows[i]["g"], rows[i]["b"], rows[i]["lum"]):
                i += 1
            if i >= N:
                break  # no more Black found

            series_start = i  # index of the first Black row (start of the 16-sample series)
            if series_start + SERIES_LEN > N:
                err(f"Series at data index {series_start} (line ~{rows[series_start]['line_no']}): "
                    f"need {SERIES_LEN} samples, have {N - series_start}. Stopping.")
                break

            # Create array of measurements (Red, Green, Blue, Black) using block midpoints
            try:
                # Offsets for the 3rd row of each block in a 16-sample series
                off_black = 0 * BLOCK_LEN + MID_IN_BLOCK  # 2
                off_red   = 1 * BLOCK_LEN + MID_IN_BLOCK  # 6
                off_green = 2 * BLOCK_LEN + MID_IN_BLOCK  # 10
                off_blue  = 3 * BLOCK_LEN + MID_IN_BLOCK  # 14

                red_rec   = rows[series_start + off_red]
                green_rec = rows[series_start + off_green]
                blue_rec  = rows[series_start + off_blue]
                black_rec = rows[series_start + off_black]  # part of the array but will be removed

                # Build array (R,G,B,Black), then remove Black
                measurements = [("R", red_rec), ("G", green_rec), ("B", blue_rec), ("BLACK", black_rec)]
                measurements = [m for m in measurements if m[0] != "BLACK"]

                # Write R/G/B with the same current
                for label, rec in measurements:
                    write_rgb(writer, rec, current, label)

                written += len(measurements)
                current += 1

                # After Blue is extracted, start searching for next Black
                i = series_start + SERIES_LEN

            except Exception as e:
                err(f"Series starting at line ~{rows[series_start]['line_no']}: failed to extract block midpoints: {e}")
                # Move forward cautiously to avoid infinite loop
                i = series_start + 1

        info(f"Wrote {written} rows up to current={min(current-1, args.max_current)}.")

if __name__ == "__main__":
    main()
