@echo off
REM DX12 Technique Discovery Agent Runner
REM This script runs the technique discovery agent for PlasmaDX

echo Starting DX12 Technique Discovery Agent...

REM Create logs directory if it doesn't exist
if not exist "logs" mkdir logs

REM Run the agent in single discovery mode
python src/agents/technique_agent_runner.py --mode single --verbose

REM Check if the command was successful
if %ERRORLEVEL% EQU 0 (
    echo Technique discovery completed successfully
    echo Check findings/technique_knowledge_base.json for results
) else (
    echo Technique discovery failed with error code %ERRORLEVEL%
    echo Check logs/technique_agent.log for details
)

pause



