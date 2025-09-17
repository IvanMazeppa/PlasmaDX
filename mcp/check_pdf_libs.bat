@echo off
echo Checking PDF library installation...
echo.

python test_pdf_libs.py

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Trying with py launcher...
    py test_pdf_libs.py
)

echo.
pause
