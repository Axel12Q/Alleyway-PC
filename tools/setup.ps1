$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    uv venv
    if ($LASTEXITCODE -ne 0) { throw 'uv venv failed' }
    uv pip sync requirements-dev.lock
    if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed' }
    & "$PSScriptRoot/setup_sameboy.ps1"
    & ./.venv/Scripts/python.exe tools/build.py
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
    & ./build/alleyway-lab.exe --smoke-test
    if ($LASTEXITCODE -ne 0) { throw 'Native smoke test failed' }
} finally {
    Pop-Location
}
