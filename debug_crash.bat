@echo off
echo ========================================
echo Debugging PlasmaDX Crash
echo ========================================
echo.

echo Capturing output to crash_log.txt...
echo.

cd /d D:\Users\dilli\AndroidStudioProjects\PlasmaDX

REM Clear previous log
if exist crash_log.txt del crash_log.txt

REM Run with output capture
echo === PlasmaDX Crash Log === > crash_log.txt
echo Date: %date% %time% >> crash_log.txt
echo ====================================== >> crash_log.txt
echo. >> crash_log.txt

REM Set environment variables
set PLASMADX_NO_DEBUG=1
set PLASMADX_DEBUG_MODE=0
set PLASMADX_DISABLE_DXR=1

REM Run and capture both stdout and stderr
build-vs2022\Debug\PlasmaDX.exe >> crash_log.txt 2>&1

echo.
echo Exit code: %errorlevel%
echo Exit code: %errorlevel% >> crash_log.txt

echo.
echo ========================================
echo Output saved to crash_log.txt
echo Opening log file...
echo ========================================

REM Open the log file
notepad crash_log.txt

pause