#!/usr/bin/env bash

# This script automates the setup and execution of ruuvi_air_rgb_ctrl.py
# It creates a virtual environment if one doesn't exist, installs dependencies,
# and then runs the main Python script.

# --- Configuration ---
VENV_DIR=".venv"
PYTHON_SCRIPT="ruuvi_air_rgb_ctrl.py"
REQUIREMENTS_FILE="requirements.txt"

# Check if the virtual environment directory exists
if [ ! -d "$VENV_DIR" ]; then
    echo "Virtual environment not found. Creating one..."

    # Create the virtual environment
    python3 -m venv "$VENV_DIR"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create virtual environment. Please ensure python3 and venv are installed."
        exit 1
    fi

    echo "Activating virtual environment..."
    source "$VENV_DIR/bin/activate"

    echo "Installing required packages from $REQUIREMENTS_FILE..."
    pip install -r "$REQUIREMENTS_FILE"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to install packages. Please check your internet connection and $REQUIREMENTS_FILE."
        # Deactivate on failure
        deactivate
        exit 1
    fi
else
    # If the environment already exists, just activate it
    echo "Activating existing virtual environment..."
    source "$VENV_DIR/bin/activate"
fi

# Set up trap to deactivate virtual environment on script exit
trap 'echo "Deactivating virtual environment."; deactivate' EXIT

# Run the Python application
echo "Starting the application: $PYTHON_SCRIPT"
python3 "$PYTHON_SCRIPT"

# Note: deactivate will be called automatically by the trap
echo "Application closed."
