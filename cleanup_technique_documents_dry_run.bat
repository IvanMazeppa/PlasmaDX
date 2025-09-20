@echo off
REM Document Cleanup Utility - Dry Run Mode
REM This script shows what documents would be moved without actually moving them

echo Document Cleanup - Dry Run Mode
echo This will show what documents would be moved without actually moving them
echo.

REM Create target directory if it doesn't exist
if not exist "agent\AdvancedTechniqueWebSearches" mkdir "agent\AdvancedTechniqueWebSearches"

echo Searching for scattered technique documents...
echo Target directory: agent\AdvancedTechniqueWebSearches
echo.

REM Run the cleanup utility in dry-run mode
python src/agents/document_cleanup.py --root . --target "agent/AdvancedTechniqueWebSearches" --dry-run

echo.
echo Dry run completed. No files were actually moved.
echo Run cleanup_technique_documents.bat to perform the actual cleanup.
echo.
pause
