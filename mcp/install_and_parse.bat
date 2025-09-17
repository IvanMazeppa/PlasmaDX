@echo off
echo Installing PDF parser for D3D12 documentation...
echo.

REM Check for pip
where pip >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: pip not found!
    echo Please install Python with pip first.
    pause
    exit /b 1
)

echo Installing PyMuPDF (best PDF parser)...
pip install pymupdf

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ✅ PyMuPDF installed successfully!
    echo.
    echo Now parsing your D3D12 PDF...
    echo.
    python parse_d3d12_pdf.py
    echo.
    echo Done! Your PDF has been parsed and added to the database.
) else (
    echo.
    echo ⚠️ PyMuPDF installation failed, trying PyPDF2...
    pip install PyPDF2
    
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ✅ PyPDF2 installed as fallback
        echo.
        echo Now parsing your D3D12 PDF...
        python parse_d3d12_pdf.py
    ) else (
        echo.
        echo ❌ Could not install PDF parser
        echo Please install manually: pip install pymupdf
    )
)

echo.
pause
