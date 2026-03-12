# run.ps1 -- Run q2rtx with vkpt renderer
. "$PSScriptRoot\_common.ps1"

$exe = Get-ExecutablePath
$baseq2 = Get-BaseQ2Dir

if (-not (Test-Path $exe)) {
    Write-DevLog "q2rtx.exe not found at: $exe" "ERR"
    Write-DevLog "Build first with: .\dev.ps1 build" "ERR"
    exit 1
}

if (-not (Test-Path $baseq2)) {
    Write-DevLog "baseq2/ directory not found. Game will likely fail." "WARN"
}

$pkz = Join-Path $baseq2 "q2rtx_media.pkz"
$pak = Get-ChildItem -Path $baseq2 -Filter "pak*.pak" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not (Test-Path $pkz) -and -not $pak) {
    Write-DevLog "No q2rtx_media.pkz or pak*.pak in baseq2/. Add game assets to baseq2/" "WARN"
}

Push-Location (Get-RepoRoot)
try {
    & $exe "+set" "vid_renderer" "vkpt"
    exit $LASTEXITCODE
} finally { Pop-Location }
