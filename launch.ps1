# Alleyway PC - Local PowerShell Launcher
# Runs tools/launch.py using python / uv environment.
# Usage:
#   .\launch.ps1
#   .\launch.ps1 "path/to/Alleyway (USA).gb"

$Python = "python"
if (Test-Path ".\.venv\Scripts\python.exe") {
    $Python = ".\.venv\Scripts\python.exe"
}

& $Python tools/launch.py @args
