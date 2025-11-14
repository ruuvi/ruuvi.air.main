#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (c) 2025, Ruuvi Innovations Ltd
# SPDX-License-Identifier: BSD-3-Clause

"""
dt-format.py - Zephyr-friendly DTS/DTSI formatter

Defaults (no flags): whitespace-preserving except for brace-driven node
indentation. Multi-line properties keep content; we normalize only the
leading indentation of continuation lines so they're consistent.

Features:
  - Indents on '{' / '}' (brace counts taken from code-only, trailing comments ignored).
  - Tabs by default; or --spaces N.
  - Optional: --sort (sort single-line properties: Zephyr-ish priority, then alpha).
  - Optional: --comment-align (align trailing comments of single-line properties).
  - Multi-line support:
      * Continuations for comma-separated lists.
      * Arrays: "= [ ... ];" with closing "];" at node indent.
      * Continuation indent normalization; optional --cont-align=equals.
  - Enforces a single blank line between sibling nodes (disable with --no-blank-between-siblings).
  - Optional: --check (runs dtc).
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Tuple, Optional

PRIORITY = [
    "compatible", "model", "status", "label", "reg", "ranges",
    "interrupt-parent", "interrupts",
    "clocks", "clock-frequency",
    "pinctrl-names", "pinctrl-0", "pinctrl-1", "pinctrl-2",
    "gpios", "gpio-controller",
    "dmas", "dma-names",
    "pwms",
    "spi-max-frequency",
    "i2c,recovery-timeout-us",
    "#address-cells", "#size-cells",
    "phandle",
]

def _is_directive(line_stripped: str) -> bool:
    """
    Treat as CPP/DT directives only if:
      - line starts with /dts-v1/, /plugin/, /include/, OR
      - line starts with '#' AND does NOT end with ';' (so '#include', '#define', etc.)
    Properties like '#address-cells;' must NOT be treated as directives.
    """
    if not line_stripped:
        return True
    if line_stripped.startswith("/dts-v1/") or line_stripped.startswith("/plugin/") or line_stripped.startswith("/include/"):
        return True
    if line_stripped.startswith("#") and not line_stripped.endswith(";"):
        return True
    return False

def _split_trailing_comment(s: str) -> Tuple[str, str]:
    """
    Split off a trailing //... or /* ... */ comment (naively, not inside quotes).
    Returns (code_part, trailing_comment_with_prefix_or_empty).
    """
    idx_slash = s.find("//")
    idx_cblock = s.find("/*")
    idx = -1
    if idx_slash != -1 and (idx_cblock == -1 or idx_slash < idx_cblock):
        idx = idx_slash
    elif idx_cblock != -1:
        idx = idx_cblock
    if idx != -1:
        return s[:idx].rstrip(), s[idx:].rstrip()
    return s.rstrip(), ""

def _split_name_value(prop_code: str) -> Tuple[str, Optional[str]]:
    """
    For 'name = value;' (possibly with spaces), return ('name', 'value;').
    For 'flag;' (no '='), return ('flag;', None).
    """
    if "=" not in prop_code:
        return prop_code.strip(), None
    name, rest = prop_code.split("=", 1)
    return name.strip(), rest.strip()

def _priority_key(name: str) -> Tuple[int, str]:
    lname = name.lower()
    if lname in PRIORITY:
        return (PRIORITY.index(lname), lname)
    return (len(PRIORITY) + 1, lname)

class Indenter:
    def __init__(self, use_spaces: bool, space_count: int):
        self.use_spaces = use_spaces
        self.space_count = space_count
        self.level = 0
    def unit(self) -> str:
        return (" " * self.space_count) if self.use_spaces else "\t"
    def pad(self) -> str:
        return self.unit() * self.level
    def pad_plus(self, extra_units: int) -> str:
        return self.pad() + (self.unit() * max(0, int(extra_units)))

def format_dts(
    content: str,
    use_spaces: bool = False,
    space_count: int = 4,
    sort_props: bool = False,
    align_comments: bool = False,
    comment_col: int = 56,
    enforce_blank_between_siblings: bool = True,
    cont_indent_units: int = 1,
    cont_align: str = "indent",  # "indent" (default) or "equals"
) -> str:
    ind = Indenter(use_spaces, space_count)
    out: List[str] = []
    prop_buf: List[Tuple[str, str, str]] = []  # (name_or_raw, value_or_None, comment)
    prev_was_node_close_at_depth: Optional[int] = None

    # Standalone block comment handling (lines that START with /* after whitespace)
    in_block_comment = False

    # Multi-line tracking
    in_prop_continuation = False   # "name = ...," lines until a ';' (code-only)
    in_bracket_block = False       # "name = [ ..." until a "];" (code-only)
    continuation_pad = ""          # computed per multi-line block
    node_pad_for_block = ""        # node indent pad for '];'

    def flush_props():
        nonlocal prop_buf, out
        if not prop_buf:
            return
        named = [p for p in prop_buf if p[1] is not None]
        ordered = prop_buf
        if sort_props and named:
            named_sorted = sorted(named, key=lambda p: _priority_key(p[0]))
            ordered = named_sorted + [p for p in prop_buf if p[1] is None]
        max_name_len = max((len(p[0]) for p in ordered if p[1] is not None), default=0)
        for (name_or_raw, val_or_none, comment) in ordered:
            pad = ind.pad()
            if val_or_none is not None:
                spaces_after = 1 if not (sort_props or align_comments) else max(1, max_name_len - len(name_or_raw) + 1)
                code = f"{name_or_raw}{' ' * spaces_after}= {val_or_none}"
            else:
                code = name_or_raw
            if align_comments and comment:
                visible_len = len(pad) + len(code)
                if visible_len + 1 < comment_col:
                    code = code + (" " * (comment_col - visible_len))
                else:
                    code = code + " "
                out.append(f"{pad}{code}{comment}")
            else:
                out.append(f"{pad}{code}{((' ' + comment) if comment else '')}")
        prop_buf = []

    def _compute_equals_pad(first_line_stripped: str, node_pad: str) -> str:
        """Pad so that continuation lines align under the first token after '=' on the first line."""
        code_only, _ = _split_trailing_comment(first_line_stripped)
        eq = code_only.find("=")
        if eq < 0:
            return node_pad + ind.unit() * cont_indent_units
        j = eq + 1
        n = len(code_only)
        if j < n and code_only[j] == " ":
            j += 1
        while j < n and code_only[j] == " ":
            j += 1
        # tabs/spaces for node indent + spaces to column after '='
        return node_pad + (" " * j)

    lines = content.splitlines()
    for raw in lines:
        line = raw.rstrip("\n").rstrip("\r").rstrip()
        stripped = line.strip()
        lstripped = line.lstrip()

        # Split trailing comment to get code-only (used for brace counting and continuation detection)
        code_only, trailing_cmt = _split_trailing_comment(stripped)

        # --- preserve standalone /* ... */ blocks verbatim ---
        if in_block_comment:
            out.append(line)
            if "*/" in line:
                in_block_comment = False
            continue
        if lstripped.startswith("/*") and "*/" not in lstripped:
            out.append(line)
            in_block_comment = True
            continue
        if lstripped.startswith("/*") and "*/" in lstripped:
            out.append(line)
            continue
        # NOTE: inline /* ... */ comments on code lines are handled normally

        # --- directives: passthrough (but DO NOT treat '#address-cells;' as directive) ---
        if _is_directive(stripped) and not code_only.endswith(";"):
            flush_props()
            out.append(line)
            continue

        # Brace counts MUST come from code-only (ignore trailing comments)
        open_count = code_only.count("{")
        close_count = code_only.count("}")
        close_leading = 1 if code_only.strip().startswith("}") else 0

        if close_leading:
            flush_props()
            ind.level = max(ind.level - close_leading, 0)

        # Detect start types (use code-only)
        starts_bracket_block = (
            "=" in code_only and "[" in code_only and "];" not in code_only
            and "{" not in code_only and "}" not in code_only
            and not code_only.startswith("&")
        )
        starts_continuation = (
            "=" in code_only and not code_only.endswith(";") and not starts_bracket_block
            and "{" not in code_only and "}" not in code_only
            and not code_only.startswith("&")
        )

        # --- Inside bracket array block ---
        if in_bracket_block:
            # Closing '];' at node indent; keep any trailing inline comment
            if code_only.strip() == "];":
                suffix = ""
                if "];" in line:
                    idx = line.find("];")
                    suffix = line[idx+2:]
                out.append(node_pad_for_block + "];" + suffix)
                in_bracket_block = False
            else:
                out.append(continuation_pad + line.lstrip())
            after_delta = open_count - (close_count - close_leading)
            if after_delta < 0:
                ind.level = max(ind.level + after_delta, 0)
            continue

        # --- Inside comma-continued property ---
        if in_prop_continuation:
            out.append(continuation_pad + line.lstrip())
            # End when the code-only part ends with ';'
            if code_only.endswith(";"):
                in_prop_continuation = False
            after_delta = open_count - (close_count - close_leading)
            if after_delta < 0:
                ind.level = max(ind.level + after_delta, 0)
            continue

        # --- Enter multi-line modes if detected ---
        if starts_bracket_block:
            node_pad_for_block = ind.pad()
            out.append(f"{node_pad_for_block}{stripped}")
            continuation_pad = (
                _compute_equals_pad(stripped, node_pad_for_block)
                if cont_align == "equals" else
                ind.pad_plus(cont_indent_units)
            )
            in_bracket_block = True
            after_delta = open_count - (close_count - close_leading)
            if after_delta < 0:
                ind.level = max(ind.level + after_delta, 0)
            continue

        if starts_continuation:
            node_pad_for_block = ind.pad()
            out.append(f"{node_pad_for_block}{stripped}")
            continuation_pad = (
                _compute_equals_pad(stripped, node_pad_for_block)
                if cont_align == "equals" else
                ind.pad_plus(cont_indent_units)
            )
            in_prop_continuation = True
            after_delta = open_count - (close_count - close_leading)
            if after_delta < 0:
                ind.level = max(ind.level + after_delta, 0)
            continue

        # Single-line property candidate? (based on code-only)
        is_property_candidate = (
            code_only.endswith(";")
            and "{" not in code_only and "}" not in code_only
            and not code_only.startswith("&")
        )
        is_node_start = code_only.endswith("{")

        # Default: emit single-line properties with node indent if not sorting/aligning
        if is_property_candidate and not (sort_props or align_comments):
            flush_props()
            out.append(f"{ind.pad()}{stripped}")
            after_delta = open_count - (close_count - close_leading)
            if after_delta < 0:
                ind.level = max(ind.level + after_delta, 0)
            if code_only == "};" or code_only.endswith("};"):
                prev_was_node_close_at_depth = ind.level
            continue

        # Sorting/aligning: buffer single-line properties
        if is_property_candidate:
            code, comment = _split_trailing_comment(stripped)
            name, value_or_none = _split_name_value(code)
            if value_or_none is not None and not value_or_none.endswith(";"):
                value_or_none += ";"
            elif value_or_none is None and not name.endswith(";"):
                name = name + ";"
            prop_buf.append((name, value_or_none, comment))
            after_delta = open_count - (close_count - close_leading)
            if after_delta < 0:
                ind.level = max(ind.level + after_delta, 0)
            continue

        # Not a property line: flush props
        flush_props()

        if is_node_start:
            if (
                enforce_blank_between_siblings
                and out
                and prev_was_node_close_at_depth is not None
                and prev_was_node_close_at_depth == ind.level
                and out[-1].strip() != ""
            ):
                out.append("")
            out.append(f"{ind.pad()}{stripped}")
            ind.level += 1
            prev_was_node_close_at_depth = None
        else:
            out.append(f"{ind.pad()}{stripped}")

        after_delta = open_count - (close_count - close_leading)
        if after_delta < 0:
            ind.level = max(ind.level + after_delta, 0)
        if code_only == "};" or code_only.endswith("};"):
            prev_was_node_close_at_depth = ind.level

    flush_props()
    return ("\n".join(out)).rstrip() + "\n"

def run_dtc_check(text: str, src_path: Path) -> Tuple[bool, str]:
    dtc = shutil.which("dtc")
    if not dtc:
        return False, "dtc not found in PATH"
    tmp = src_path.with_suffix(src_path.suffix + ".tmp.dts")
    tmp.write_text(text, encoding="utf-8")
    try:
        res = subprocess.run(
            [dtc, "-I", "dts", "-O", "dtb", str(tmp), "-o", "/dev/null"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        ok = (res.returncode == 0)
        msg = res.stderr.strip() or res.stdout.strip()
        return ok, msg
    finally:
        try:
            tmp.unlink()
        except Exception:
            pass

def main():
    ap = argparse.ArgumentParser(description="Zephyr-friendly DTS/DTSI formatter")
    ap.add_argument("file", help="Input .dts/.dtsi/.overlay file")
    grp = ap.add_mutually_exclusive_group()
    grp.add_argument("--spaces", type=int, metavar="N", help="Use N spaces for indent")
    grp.add_argument("--tabs", action="store_true", help="Use tabs for indent (default)")
    ap.add_argument("--inplace", action="store_true", help="Modify file in place")
    ap.add_argument("--sort", action="store_true", help="Sort (single-line) properties within nodes")
    ap.add_argument("--comment-align", action="store_true", help="Align trailing comments (single-line properties)")
    ap.add_argument("--comment-col", type=int, default=56, help="Column to align trailing comments (default: 56)")
    ap.add_argument("--no-blank-between-siblings", action="store_true",
                    help="Do not enforce single blank line between sibling nodes")
    ap.add_argument("--cont-indent", type=int, default=1,
                    help="Continuation indent units (default: 1)")
    ap.add_argument("--cont-align", choices=["indent", "equals"], default="indent",
                    help="Continuation alignment: 'indent' (node indent + units) or 'equals'")
    ap.add_argument("--check", action="store_true", help="Validate with dtc after formatting")
    args = ap.parse_args()

    path = Path(args.file)
    text = path.read_text(encoding="utf-8")

    formatted = format_dts(
        text,
        use_spaces=(args.spaces is not None and not args.tabs),
        space_count=(args.spaces or 4),
        sort_props=args.sort,
        align_comments=args.comment_align,
        comment_col=args.comment_col,
        enforce_blank_between_siblings=(not args.no_blank_between_siblings),
        cont_indent_units=args.cont_indent,
        cont_align=args.cont_align,
    )

    if args.check:
        ok, msg = run_dtc_check(formatted, path)
        if not ok:
            print(f"[dtc] validation failed:\n{msg}", file=sys.stderr)
        elif msg:
            print(f"[dtc] {msg}", file=sys.stderr)

    if args.inplace:
        path.write_text(formatted, encoding="utf-8")
    else:
        sys.stdout.write(formatted)

if __name__ == "__main__":
    main()
