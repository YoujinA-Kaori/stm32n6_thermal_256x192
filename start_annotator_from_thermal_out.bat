@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "POWERSHELL_SCRIPT=%SCRIPT_DIR%thermal_ai\scripts\start_annotator_from_thermal_out.ps1"

if not exist "%POWERSHELL_SCRIPT%" (
    echo Annotator launcher not found:
    echo   %POWERSHELL_SCRIPT%
    pause
    exit /b 1
)

powershell -ExecutionPolicy Bypass -File "%POWERSHELL_SCRIPT%"
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" (
    echo.
    echo Annotator exited with error code %EXIT_CODE%.
    pause
)

exit /b %EXIT_CODE%
