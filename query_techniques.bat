@echo off
REM DX12 Technique Query Tool
REM This script allows you to query the technique knowledge base

if "%1"=="" (
    echo Usage: query_techniques.bat "your search query"
    echo Example: query_techniques.bat "volumetric rendering"
    echo Example: query_techniques.bat "acceleration structure"
    pause
    exit /b 1
)

echo Searching for techniques related to: %1
echo.

REM Run the query
python src/agents/technique_agent_runner.py --mode query --query "%1" --limit 10

echo.
echo Query completed. Check the results above.
pause



