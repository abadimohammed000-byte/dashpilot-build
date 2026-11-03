$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $projectRoot

if (-not (Get-Command geode -ErrorAction SilentlyContinue)) {
    throw 'Geode CLI was not found. Install geode-cli and reopen PowerShell.'
}
if (-not $env:GEODE_SDK) {
    throw 'GEODE_SDK is not set. Run geode sdk install for the SDK matching mod.json, then reopen PowerShell.'
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found on PATH.'
}

Write-Host "Building DashPilot AI with SDK: $env:GEODE_SDK" -ForegroundColor Cyan
geode build
if ($LASTEXITCODE -ne 0) {
    throw "geode build failed with exit code $LASTEXITCODE"
}

$out = Join-Path $projectRoot 'build'
if (Test-Path $out) {
    Write-Host "Build output: $out" -ForegroundColor Green
    Start-Process explorer.exe $out
} else {
    Write-Warning 'Build completed, but the usual build folder was not found. Check the Geode CLI output above.'
}
