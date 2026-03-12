# clean-build.ps1 -- Wipe build/, recreate, configure, build
. "$PSScriptRoot\_common.ps1"

$buildDir = Get-BuildDir
$repoRoot = Get-RepoRoot

$buildAbs = (Resolve-Path $buildDir).Path 2>$null
$repoAbs = (Resolve-Path $repoRoot).Path
if ($buildAbs -and $buildAbs -notlike "$repoAbs*") {
    Write-DevLog "Build path is outside repo. Aborting." "ERR"
    exit 1
}

Write-DevLog "Performing full clean-build..."

if (Test-Path $buildDir) {
    Remove-Item -Path $buildDir -Recurse -Force
    Write-DevLog "Removed build directory"
}

& "$PSScriptRoot\configure.ps1"
if ($LASTEXITCODE -ne 0) { exit 1 }

& "$PSScriptRoot\build.ps1"
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-DevLog "Clean-build complete."
