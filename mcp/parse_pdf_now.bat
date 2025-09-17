@echo off
echo Running simplified PDF parser...
echo This will scan the first 100 pages of your D3D12 PDF
echo.

python parse_pdf_simple.py

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Trying with py launcher...
    py parse_pdf_simple.py
)

echo.
pause
