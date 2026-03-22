# Download and unpack Streamline SDK v2.10.3
# Target: <repo>\Third Parties\NVIDIA
# Release: https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.3

$ErrorActionPreference = "Stop"

$DownloadUrl    = "https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.10.3/streamline-sdk-v2.10.3.zip"
$ExpectedSha256 = "8564646ca6dd7c3960370d05b06054549563f499bfb93d160b46b7494a47bb3c"

# Resolve target relative to this script's location so it works on any machine.
# Assumes the script lives in the repo root. If it lives elsewhere, adjust the relative path below.
$RepoRoot  = Split-Path -Parent $MyInvocation.MyCommand.Path
$TargetDir = Join-Path $RepoRoot "Third Parties\NVIDIA"
$ZipPath   = Join-Path $TargetDir "streamline-sdk-v2.10.3.zip"

Write-Host "[INFO] Creating target directory: $TargetDir"
New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null

Write-Host "[INFO] Downloading Streamline SDK v2.10.3..."
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $DownloadUrl -OutFile $ZipPath -UseBasicParsing

Write-Host "[INFO] Verifying SHA256..."
$ActualSha256 = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualSha256 -ne $ExpectedSha256.ToLowerInvariant()) {
    Write-Error "SHA256 mismatch. Expected: $ExpectedSha256  Got: $ActualSha256"
    exit 1
}
Write-Host "[INFO] SHA256 verified OK."

Write-Host "[INFO] Extracting..."
Expand-Archive -Path $ZipPath -DestinationPath $TargetDir -Force

Write-Host "[INFO] Removing downloaded zip..."
Remove-Item -Path $ZipPath -Force

Write-Host "[INFO] Done. Streamline SDK v2.10.3 extracted to: $TargetDir"