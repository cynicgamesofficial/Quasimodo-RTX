# rebuild.ps1 -- Clean build artifacts, reconfigure, rebuild
. "$PSScriptRoot\_common.ps1"

Write-DevLog "Rebuilding Q2RTX..."

& "$PSScriptRoot\clean.ps1"
if ($LASTEXITCODE -ne 0) { exit 1 }

& "$PSScriptRoot\configure.ps1"
if ($LASTEXITCODE -ne 0) { exit 1 }

& "$PSScriptRoot\build.ps1"
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-DevLog "Rebuild complete."
