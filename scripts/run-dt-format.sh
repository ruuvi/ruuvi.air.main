#!/usr/bin/env bash
# Copyright (c) 2025, Ruuvi Innovations Ltd
# SPDX-License-Identifier: BSD-3-Clause
# -----------------------------------------------------------------------------
# run-dt-format.sh
# -----------------------------------------------------------------------------
# Recursively format DeviceTree files (*.dts, *.dtsi, *.overlay)
# using dt-format.py.
#
# Supports:
#   --inplace   : rewrite files
#   --diff      : show diffs, exit 1 if formatting needed (for CI)
#   --tabs / --spaces N : control indentation
#   --check     : run dtc validation
#   --ignore-file FILE  : file with ignore patterns (default: .clang-format-ignore)
#
# Example:
#   scripts/run-dt-format.sh --inplace
#   scripts/run-dt-format.sh --diff
#   scripts/run-dt-format.sh --spaces 4 --inplace ./boards ./dts
# -----------------------------------------------------------------------------

set -euo pipefail

DT_FORMAT="${DT_FORMAT:-./scripts/dt-format.py}"
IGNORE_FILE_DEFAULT="${IGNORE_FILE_DEFAULT:-.clang-format-ignore}"
INPLACE=false
CHECK=false
SHOW_DIFF=false
RECURSIVE=false
INDENT_FLAGS=(--tabs)
IGNORE_FILE="$IGNORE_FILE_DEFAULT"
PATHS=()

# --- Parse args ---
while (($#)); do
  case "$1" in
    --inplace) INPLACE=true; shift ;;
    --diff) SHOW_DIFF=true; shift ;;
    -r|--recursive) RECURSIVE=true; shift ;;
    --tabs) INDENT_FLAGS=(--tabs); shift ;;
    --spaces)
      [[ $# -lt 2 ]] && { echo "error: --spaces needs a number" >&2; exit 2; }
      INDENT_FLAGS=(--spaces "$2"); shift 2 ;;
    --check) CHECK=true; shift ;;
    --ignore-file)
      [[ $# -lt 2 ]] && { echo "error: --ignore-file needs a path" >&2; exit 2; }
      IGNORE_FILE="$2"; shift 2 ;;
    -h|--help)
      cat <<'EOF'
Usage: run-dt-format.sh [OPTIONS] [PATH ...]
Format .dts/.dtsi/.overlay files. Non-recursive by default.

Options:
  --inplace               Rewrite files in place
  --diff                  Show unified diff; exit 1 if changes needed
  -r, --recursive         Recurse into directories
  --tabs                  Use tabs for indentation (default)
  --spaces N              Use N spaces for indentation
  --check                 Run dtc validation inside dt-format.py
  --ignore-file FILE      Patterns file (default: .clang-format-ignore)
  -h, --help              Show this help
EOF
      exit 0 ;;
    --) shift; break ;;
    -*)
      echo "error: unknown flag: $1" >&2; exit 2 ;;
    *)
      PATHS+=("$1"); shift ;;
  esac
done
(( $# )) && PATHS+=("$@")
((${#PATHS[@]})) || PATHS=(.)

# --- Verify dependencies ---
[[ -f "$DT_FORMAT" ]] || { echo "error: dt-format.py not found at $DT_FORMAT" >&2; exit 2; }

# --- Build ignore regex ---
IGNORE_RE=""
if [[ -f "$IGNORE_FILE" ]]; then
  mapfile -t _patterns < <(grep -vE '^\s*(#|$)' "$IGNORE_FILE" || true)
  if ((${#_patterns[@]} > 0)); then
    _regex_parts=()
    for p in "${_patterns[@]}"; do
      p="${p#"${p%%[![:space:]]*}"}"
      p="${p%"${p##*[![:space:]]}"}"
      [[ -z "$p" ]] && continue
      esc="$(printf '%s' "$p" | sed -e 's/[.[\]{}()^$+?|]/\\&/g' -e 's/\*/.*/g')"
      _regex_parts+=("$esc")
    done
    ((${#_regex_parts[@]})) && IGNORE_RE="(${_regex_parts[*]// /|})"
  fi
fi

# --- Gather files (non-recursive by default) ---
# Build a null-separated list into DTS_FILES
collect_tmp="$(mktemp)"
> "$collect_tmp"

append_find_results() {
  local dir="$1"
  local depth_flag=()
  if ! $RECURSIVE; then depth_flag=( -maxdepth 1 ); fi
  # shellcheck disable=SC2016
  find "$dir" "${depth_flag[@]}" -type f \
    \( -name '*.dts' -o -name '*.dtsi' -o -name '*.overlay' \) -print0
}

for p in "${PATHS[@]}"; do
  if [[ -f "$p" ]]; then
    case "$p" in
      *.dts|*.dtsi|*.overlay) printf '%s\0' "$p" >>"$collect_tmp" ;;
      *) : ;;  # skip non-DTS files
    esac
  elif [[ -d "$p" ]]; then
    append_find_results "$p" >>"$collect_tmp"
  else
    # If it doesn't exist, ignore (keeps behavior forgiving like find)
    :
  fi
done

# Load list
mapfile -d '' DTS_FILES < "$collect_tmp"
rm -f "$collect_tmp"

# Filter ignored
if [[ -n "$IGNORE_RE" && ${#DTS_FILES[@]} -gt 0 ]]; then
  mapfile -d '' DTS_FILES < <(printf '%s\0' "${DTS_FILES[@]}" | grep -z -Ev "$IGNORE_RE" || true)
fi
((${#DTS_FILES[@]})) || exit 0

# --- Prepare flags passed to dt-format.py ---
FORMAT_FLAGS=("${INDENT_FLAGS[@]}")
$INPLACE && FORMAT_FLAGS+=(--inplace)
$CHECK && FORMAT_FLAGS+=(--check)

# --- Mode handling ---
if $SHOW_DIFF; then
  HAS_DIFF=0
  for f in "${DTS_FILES[@]}"; do
    orig=$(mktemp) ; newf=$(mktemp)
    cp "$f" "$orig"
    python3 "$DT_FORMAT" "${FORMAT_FLAGS[@]}" "$f" >"$newf"
    if ! diff -u --color=always "$orig" "$newf"; then
      HAS_DIFF=1
    fi
    rm -f "$orig" "$newf"
  done
  exit $HAS_DIFF
elif $INPLACE; then
  printf '%s\0' "${DTS_FILES[@]}" \
    | xargs -0 -n 1 -P "$(getconf _NPROCESSORS_ONLN || echo 4)" \
        python3 "$DT_FORMAT" "${FORMAT_FLAGS[@]}"
fi
