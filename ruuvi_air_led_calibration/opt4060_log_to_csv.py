#!/usr/bin/env python3
import argparse
import csv
import re
import sys

# Example line:
# [00:01:33.855,560] sensors: [main/0x20007568/0] OPT4060: R=384.000000, G=1184.000000, B=62.400002, L=5.727600
# [00:01:34.355,712] sensors: [main/0x20007568/0] OPT4060: R=nan, G=1192.000000, B=67.599998, L=5.719000
LINE_RE = re.compile(
    r"""^.*?\[
        (?P<ts>[^\]]+)           # timestamp inside brackets
      \].*?
      OPT4060:\s*
      R=(?P<R>-?(?:\d+(?:\.\d+)?|nan)),\s*
      G=(?P<G>-?(?:\d+(?:\.\d+)?|nan)),\s*
      B=(?P<B>-?(?:\d+(?:\.\d+)?|nan)),\s*
      L=(?P<L>-?(?:\d+(?:\.\d+)?|nan))
      .*?$
    """,
    re.IGNORECASE | re.VERBOSE,
)


def parse_line(line):
    match = LINE_RE.match(line)
    if not match:
        return None

    timestamp = match.group("ts")
    # Normalize timestamp by removing comma separator if present
    # Convert "00:01:33.855,560" to "00:01:33.855560"
    timestamp = timestamp.replace(',', '')

    values = {}

    for channel in ['R', 'G', 'B', 'L']:
        value_str = match.group(channel)

        # Handle 'nan' values by converting to empty string for CSV compatibility
        if value_str.lower() == 'nan':
            value = ""  # Empty string for Excel/Sheets compatibility
        else:
            value = float(value_str)

        # Map L to LUM for consistency
        if channel == 'L':
            values['LUM'] = value
        else:
            values[channel] = value

    return {
        "ts": timestamp,
        "R": values["R"],
        "G": values["G"],
        "B": values["B"],
        "LUM": values["LUM"],
    }


def write_row(writer, record):
    # record: dict with keys ts,R,G,B,LUM
    writer.writerow([record["ts"], record["R"], record["G"], record["B"], record["LUM"]])

def main():
    ap = argparse.ArgumentParser(
        description="Convert OPT4060 log to CSV (timestamp,R,G,B,luminosity)."
    )
    ap.add_argument("input", help="Path to input log file (use - for stdin)")
    ap.add_argument("output", help="Path to output CSV file (use - for stdout)")
    ap.add_argument(
        "--accept-invalid",
        action="store_true",
        help="Include lines with valid=0 (default: ignore them).",
    )
    args = ap.parse_args()

    # Input stream
    if args.input == "-":
        fin = sys.stdin
        close_in = False
    else:
        fin = open(args.input, "r", encoding="utf-8", errors="replace")
        close_in = True

    # Output stream
    if args.output == "-":
        fout = sys.stdout
        close_out = False
    else:
        fout = open(args.output, "w", newline="", encoding="utf-8")
        close_out = True

    writer = csv.writer(fout)
    writer.writerow(["timestamp", "R", "G", "B", "luminosity"])

    # We accumulate 4 consecutive relevant lines to form one measurement.
    current = {"ts": None, "R": None, "G": None, "B": None, "LUM": None}

    for line in fin:
        if parsed := parse_line(line):
            write_row(writer, parsed)

    # Optionally, you could warn on leftover partial group
    # if have_any:
    #     print("Warning: incomplete measurement at end of file was ignored.", file=sys.stderr)

    if close_in:
        fin.close()
    if close_out:
        fout.close()

if __name__ == "__main__":
    main()
