# thirdparties.ps1 -- Download Streamline + NRD/NRI into Third Parties\ (repo root script)
. "$PSScriptRoot\_common.ps1"

$root = Get-RepoRoot
$bootstrap = Join-Path $root "download-thirdparties.ps1"
if (-not (Test-Path $bootstrap)) {
    Write-Error "Missing download script: $bootstrap"
    exit 1
}

Write-DevLog "Running third-party download bootstrap"
& $bootstrap
exit $LASTEXITCODE
