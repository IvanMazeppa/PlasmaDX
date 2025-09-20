@echo off
REM Document Cleanup Utility for DX12 Technique Agent
REM This script finds and moves scattered technique-related documents
REM to the centralized agent/AdvancedTechniqueWebSearches folder

echo Starting Document Cleanup for DX12 Technique Agent...
echo.

REM Create target directory if it doesn't exist
if not exist "agent\AdvancedTechniqueWebSearches" mkdir "agent\AdvancedTechniqueWebSearches"

echo Searching for scattered technique documents...
echo Target directory: agent\AdvancedTechniqueWebSearches
echo.

REM Run the cleanup utility
python src/agents/document_cleanup.py --root . --target "agent/AdvancedTechniqueWebSearches"

REM Check if the command was successful
if %ERRORLEVEL% EQU 0 (
    echo.
    echo Document cleanup completed successfully
    echo Check agent/AdvancedTechniqueWebSearches/document_cleanup_report.md for details
) else (
    echo.
    echo Document cleanup failed with error code %ERRORLEVEL%
    echo Check the output above for details
)

echo.
pause
