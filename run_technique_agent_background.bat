@echo off
REM DX12 Technique Discovery Agent - Background Mode
REM This script runs the technique discovery agent continuously in the background

echo Starting DX12 Technique Discovery Agent in Background Mode...
echo Press Ctrl+C to stop the agent

REM Create logs directory if it doesn't exist
if not exist "logs" mkdir logs

REM Run the agent in background mode (24 hour intervals)
python src/agents/technique_agent_runner.py --mode background --interval 24 --verbose

echo Agent stopped
pause




