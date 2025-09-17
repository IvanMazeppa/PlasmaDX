@echo off
REM Run PlasmaDX and save console output to timestamped log file

REM Get current date and time for log filename
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "YY=%dt:~2,2%"
set "YYYY=%dt:~0,4%"
set "MM=%dt:~4,2%"
set "DD=%dt:~6,2%"
set "HH=%dt:~8,2%"
set "Min=%dt:~10,2%"
set "Sec=%dt:~12,2%"

set "logfile=logs\plasmadx_%YYYY%%MM%%DD%_%HH%%Min%%Sec%.log"

echo Running PlasmaDX and logging to %logfile%
echo ==========================================

REM Create logs directory if it doesn't exist
if not exist logs mkdir logs

REM Run the executable and capture output
echo Starting PlasmaDX at %date% %time% > %logfile%
echo ========================================== >> %logfile%
build-vs2022\Debug\PlasmaDX.exe >> %logfile% 2>&1
set exitcode=%errorlevel%

echo ========================================== >> %logfile%
echo Exited with code: %exitcode% at %date% %time% >> %logfile%

echo.
echo Process exited with code: %exitcode%
echo Log saved to: %logfile%
echo.
type %logfile%