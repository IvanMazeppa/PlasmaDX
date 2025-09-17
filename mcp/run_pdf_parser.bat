@echo off
cd /d "D:\Users\dilli\AndroidStudioProjects\PlasmaDX\mcp"
echo Running D3D12 PDF parser...
echo.

python parse_d3d12_pdf.py

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo If you see an error, try:
    echo   py parse_d3d12_pdf.py
    echo.
    py parse_d3d12_pdf.py
)

pause
