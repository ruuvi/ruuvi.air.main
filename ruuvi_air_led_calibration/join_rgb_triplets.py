#!/usr/bin/env python3
import argparse
import csv
import sys

EPS = 1e-12

def err(msg: str):
    print(f"[ERROR] {msg}", file=sys.stderr)

def info(msg: str):
    print(f"[INFO] {msg}", file=sys.stderr)

def parse_args():
    ap = argparse.ArgumentParser(
        description="Join 3 rows (R,G,B) per current into a single row, including luminosity."
    )
    ap.add_argument("input", help="Input CSV: timestamp,Current_R,Current_G,Current_B,R,G,B,luminosity")
    ap.add_argument("output", help="Output CSV: Current,R_R,R_G,R_B,R_L,G_R,G_G,G_B,G_L,B_R,B_G,B_B,B_L")
    return ap.parse_args()

def main():
    args = parse_args()

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
        rdr = csv.reader(fin)
        try:
            header = next(rdr)
        except StopIteration:
            err("Input CSV is empty.")
            sys.exit(1)
        except Exception as e:
            err(f"Failed to read CSV header: {e}")
            sys.exit(1)

        # Column indices (case-insensitive)
        hmap = {name.strip().lower(): i for i, name in enumerate(header)}
        required = ["current_r", "current_g", "current_b", "r", "g", "b", "luminosity"]
        missing = [c for c in required if c not in hmap]
        if missing:
            err(f"Missing required columns: {', '.join(missing)}")
            sys.exit(2)

        i_cur_r = hmap["current_r"]
        i_cur_g = hmap["current_g"]
        i_cur_b = hmap["current_b"]
        i_r = hmap["r"]
        i_g = hmap["g"]
        i_b = hmap["b"]
        i_l = hmap["luminosity"]

        # Group storage: {current:int -> {"R":(r,g,b,l), "G":(...), "B":(...)}}
        groups = {}
        line_no = 1  # header at line 1
        malformed = 0

        for row in rdr:
            line_no += 1
            # guard width
            need_max = max(i_cur_r, i_cur_g, i_cur_b, i_r, i_g, i_b, i_l)
            if len(row) <= need_max:
                malformed += 1
                err(f"Line {line_no}: not enough columns (have {len(row)}, need at least {need_max+1}). Skipped.")
                continue

            try:
                cur_r = float(row[i_cur_r].strip())
                cur_g = float(row[i_cur_g].strip())
                cur_b = float(row[i_cur_b].strip())
                r_val = float(row[i_r].strip())
                g_val = float(row[i_g].strip())
                b_val = float(row[i_b].strip())
                l_val = float(row[i_l].strip())
            except Exception as e:
                malformed += 1
                err(f"Line {line_no}: parse error: {e}. Skipped.")
                continue

            on_r = cur_r > EPS
            on_g = cur_g > EPS
            on_b = cur_b > EPS
            active_count = int(on_r) + int(on_g) + int(on_b)

            if active_count != 1:
                malformed += 1
                err(f"Line {line_no}: expected exactly one of Current_R/G/B > 0, got R={cur_r}, G={cur_g}, B={cur_b}. Skipped.")
                continue

            if on_r:
                led = "R"
                current = int(round(cur_r))
            elif on_g:
                led = "G"
                current = int(round(cur_g))
            else:
                led = "B"
                current = int(round(cur_b))

            if current <= 0:
                malformed += 1
                err(f"Line {line_no}: non-positive current {current}. Skipped.")
                continue

            grp = groups.setdefault(current, {})
            if led in grp:
                err(f"Line {line_no}: duplicate {led} entry for current={current}. Overwriting previous value.")
            grp[led] = (r_val, g_val, b_val, l_val)

        # Write output
        writer = csv.writer(fout)
        writer.writerow([
            "Current",
            "R_R","R_G","R_B","R_L",
            "G_R","G_G","G_B","G_L",
            "B_R","B_G","B_B","B_L"
        ])

        # Emit only complete triplets; warn on incomplete
        for current in sorted(groups.keys()):
            grp = groups[current]
            missing_leds = [c for c in ("R", "G", "B") if c not in grp]
            if missing_leds:
                err(f"Current {current}: missing measurements for {','.join(missing_leds)}. Skipped.")
                continue

            (RR, RG, RB, RL) = grp["R"]
            (GR, GG, GB, GL) = grp["G"]
            (BR, BG, BB, BL) = grp["B"]
            writer.writerow([current, RR, RG, RB, RL, GR, GG, GB, GL, BR, BG, BB, BL])

    if malformed:
        info(f"Finished with {malformed} malformed/ambiguous row(s) skipped.")

if __name__ == "__main__":
    main()
