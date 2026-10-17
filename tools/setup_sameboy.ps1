$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cache = Join-Path $root '.tools'
New-Item -ItemType Directory -Force $cache | Out-Null
$archive = Join-Path $cache 'sameboy-1.0.4.zip'
Invoke-WebRequest 'https://github.com/LIJI32/SameBoy/releases/download/v1.0.4/sameboy_winsdl_v1.0.4.zip' -OutFile $archive
$hash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne '777a29211e17443435dd0956b7ab775e6e9f40ee859ed4f2f14b43f7587d5a38') {
    throw 'SameBoy SHA256 mismatch; archive not extracted.'
}
Expand-Archive $archive (Join-Path $cache 'sameboy') -Force
Write-Output 'SameBoy 1.0.4 ready in .tools/sameboy; Ctrl+C opens its debugger.'
