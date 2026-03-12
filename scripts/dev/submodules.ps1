# submodules.ps1 -- Initialize and update git submodules
. "$PSScriptRoot\_common.ps1"

$root = Get-RepoRoot

Write-DevLog "Updating submodules..."

Push-Location $root
try {
    & git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { exit 1 }
    Write-DevLog "Submodules updated. Run .\dev.ps1 configure"
} finally { Pop-Location }
