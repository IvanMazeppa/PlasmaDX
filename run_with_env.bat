@echo off
echo Setting D3D12 Agility SDK environment variables...
set D3D12SDKVersion=616
set D3D12SDKPath=D3D12\
echo D3D12SDKVersion=%D3D12SDKVersion%
echo D3D12SDKPath=%D3D12SDKPath%

cd Debug
echo Starting PlasmaDX with environment variables...
PlasmaDX.exe
echo Exit code: %ERRORLEVEL%
pause