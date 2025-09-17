cd Debug
try {
    Write-Host "Starting PlasmaDX..."
    .\PlasmaDX.exe
    Write-Host "Application exited normally"
} catch {
    Write-Host "Exception caught: $($_.Exception.Message)"
}
Write-Host "Exit code: $LASTEXITCODE"