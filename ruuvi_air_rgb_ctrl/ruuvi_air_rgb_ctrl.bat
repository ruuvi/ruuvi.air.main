@echo off
:: This batch script automates the setup and execution of the ruuvi_air_rgb_ctrl.py on Windows.
:: It creates a virtual environment if one doesn't exist, installs dependencies,
:: and then runs the main Python script.

:: --- Configuration ---
set VENV_DIR=.venv
set PYTHON_SCRIPT=ruuvi_air_rgb_ctrl.py
set REQUIREMENTS_FILE=requirements.txt

:: Check if the virtual environment directory exists
if not exist "%VENV_DIR%" (
    echo Virtual environment not found. Creating one...

    :: Create the virtual environment
    python -m venv "%VENV_DIR%"
    if %errorlevel% neq 0 (
        echo Error: Failed to create virtual environment. Please ensure python and venv are in your PATH.
        goto :eof
    )

    echo Activating virtual environment...
    call "%VENV_DIR%\Scripts\activate.bat"

    echo Installing required packages from %REQUIREMENTS_FILE%...
    pip install -r "%REQUIREMENTS_FILE%"
    if %errorlevel% neq 0 (
        echo Error: Failed to install packages. Please check your internet connection and %REQUIREMENTS_FILE%.
        :: Deactivate on failure
        call "%VENV_DIR%\Scripts\deactivate.bat"
        goto :eof
    )
) else (
    :: If the environment already exists, just activate it
    echo Activating existing virtual environment...
    call "%VENV_DIR%\Scripts\activate.bat"
)


:: Run the Python application
echo Starting the application: %PYTHON_SCRIPT%
python "%PYTHON_SCRIPT%"

:: Deactivate the virtual environment when the script finishes
echo Application closed. Deactivating virtual environment.
call "%VENV_DIR%\Scripts\deactivate.bat"
