# build.ps1 -- Build Q2RTX (RelWithDebInfo, parallel)
. "$PSScriptRoot\_common.ps1"

$root = Get-RepoRoot
$buildDir = Get-BuildDir

Write-DevLog "Building Q2RTX..."

if (-not (Test-Path $buildDir)) {
    Write-DevLog "Build directory does not exist. Run: .\dev.ps1 configure" "ERR"
    exit 1
}

Push-Location $root
try {
    & cmake --build build --config RelWithDebInfo --parallel
    if ($LASTEXITCODE -ne 0) { exit 1 }
    $exe = Get-ExecutablePath
    if (Test-Path $exe) {
        Write-DevLog "Build succeeded."
        Write-DevLog "Executable: $exe"
    } else {
        Write-DevLog "Build reported success but q2rtx.exe not found at expected path" "WARN"
    }
} finally { Pop-Location }
