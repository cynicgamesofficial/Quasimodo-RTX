# clean.ps1 -- Remove generated files inside build/ (never repo source files)
. "$PSScriptRoot\_common.ps1"

$buildDir = Get-BuildDir

if (-not (Test-Path $buildDir)) {
    Write-DevLog "Build directory does not exist. Nothing to clean."
    exit 0
}

$buildDirAbs = (Resolve-Path $buildDir).Path
$repoRoot = (Resolve-Path (Get-RepoRoot)).Path

if ($buildDirAbs -notlike "$repoRoot*") {
    Write-DevLog "Build path is outside repo. Aborting." "ERR"
    exit 1
}

Write-DevLog "Cleaning build artifacts in $buildDir..."
Get-ChildItem -Path $buildDir -Recurse -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
Get-ChildItem -Path $buildDir -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

Write-DevLog "Clean complete. Run .\dev.ps1 configure to reconfigure."
