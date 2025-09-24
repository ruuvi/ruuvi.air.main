#!/usr/bin/env bash
set -Eeuo pipefail  # -E keeps ERR traps in functions; -e exits on error; -u unset vars; pipefail for pipelines

VENV_DIR=".venv"
REQUIREMENTS_FILE="requirements.txt"

cleanup() {
  local status=$?
  # Deactivate only if we're actually inside a venv
  if [[ -n "${VIRTUAL_ENV:-}" ]]; then
    echo "Deactivating Python virtual environment."
    # deactivate is a shell function defined by 'activate'
    if type -t deactivate >/dev/null 2>&1; then
      deactivate
    else
      # Fallback (shouldn't happen): manually unwind PATH and vars
      PATH="${PATH#${VENV_DIR}/bin:}"
      unset VIRTUAL_ENV || true
    fi
  fi
  exit $status
}
trap cleanup EXIT  # runs on normal exit and on error (because -e will exit)

# Create venv if needed
if [[ ! -d "$VENV_DIR" ]]; then
  echo "Virtual environment not found. Creating one..."
  python3 -m venv "$VENV_DIR"
  # Activate venv
  # shellcheck source=/dev/null
  source "$VENV_DIR/bin/activate"

  # Install deps (will trigger EXIT trap + deactivate on failure)
  pip install -r "$REQUIREMENTS_FILE"
else  
  # Activate venv
  # shellcheck source=/dev/null
  source "$VENV_DIR/bin/activate"
fi

set -x  # enable verbose command tracing
# Any failure triggers EXIT trap + deactivate
python3 ./opt4060_log_to_csv.py stage_0_led_test.log stage_1_calibration_data.csv
python3 ./opt4060_filter.py stage_1_calibration_data.csv stage_2_filtered_data.csv
python3 ./join_rgb_triplets.py stage_2_filtered_data.csv stage_3_joined_rgbw.csv
python3 ./opt4060_approximate_red.py stage_3_joined_rgbw.csv stage_4_with_approximated_red.csv
python3 ./luminosity_solver.py stage_4_with_approximated_red.csv stage_5_result.csv
python3 ./emit_led_calibration_c.py stage_5_result.csv ../src/led_calibration
set +x  # disable verbose command tracing

