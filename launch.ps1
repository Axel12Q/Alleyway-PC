# Alleyway PC - Local PowerShell Launcher
# Runs tools/launch.py using python / uv environment.
# Usage:
#   .\launch.ps1
#   .\launch.ps1 "path/to/Alleyway (USA).gb"

$Python = "python"
$VenvPython = Join-Path -Path $PSScriptRoot -ChildPath ".venv\Scripts\python.exe"
if (Test-Path -LiteralPath $VenvPython) {
    $Python = $VenvPython
}

$LaunchScript = Join-Path -Path $PSScriptRoot -ChildPath "tools\launch.py"
& $Python $LaunchScript @args
