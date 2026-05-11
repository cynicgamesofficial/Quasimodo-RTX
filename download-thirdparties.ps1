# Download and unpack third-party SDKs into Third Parties\
# - Streamline SDK v2.10.3 -> Third Parties\NVIDIA
# - DLSS SDK v310.6.0      -> Third Parties\DLSS
# - NRD SDK v4.17.2        -> Third Parties\NRD       (https://github.com/NVIDIA-RTX/NRD) — NRD_ROOT
# - NRI (NRD dependency)   -> Third Parties\NRD\NRI   (https://github.com/NVIDIA-RTX/NRI) — matches NRD v4.17.2 / tag v178

param(
    [switch]$DlssOnly
)

$ErrorActionPreference = "Stop"

# Resolve paths relative to this script (repo root).
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# --- Streamline ----------------------------------------------------------
if (-not $DlssOnly) {
    $StreamlineUrl    = "https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.10.3/streamline-sdk-v2.10.3.zip"
    $StreamlineSha256 = "8564646ca6dd7c3960370d05b06054549563f499bfb93d160b46b7494a47bb3c"
    $NvidiaDir        = Join-Path $RepoRoot "Third Parties\NVIDIA"
    $StreamlineZip    = Join-Path $NvidiaDir "streamline-sdk-v2.10.3.zip"

    Write-Host "[INFO] Streamline: creating target directory: $NvidiaDir"
    New-Item -ItemType Directory -Path $NvidiaDir -Force | Out-Null

    Write-Host "[INFO] Streamline: downloading SDK v2.10.3..."
    Invoke-WebRequest -Uri $StreamlineUrl -OutFile $StreamlineZip -UseBasicParsing

    Write-Host "[INFO] Streamline: verifying SHA256..."
    $hash = (Get-FileHash -Path $StreamlineZip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $StreamlineSha256.ToLowerInvariant()) {
        Write-Error "Streamline SHA256 mismatch. Expected: $StreamlineSha256  Got: $hash"
        exit 1
    }
    Write-Host "[INFO] Streamline: SHA256 verified OK."

    Write-Host "[INFO] Streamline: extracting..."
    Expand-Archive -Path $StreamlineZip -DestinationPath $NvidiaDir -Force

    Write-Host "[INFO] Streamline: removing downloaded zip..."
    Remove-Item -Path $StreamlineZip -Force

    Write-Host "[INFO] Streamline: done. Extracted to: $NvidiaDir"
} else {
    Write-Host "[INFO] DLSS-only mode enabled; skipping Streamline download."
}

# --- DLSS ----------------------------------------------------------------
$DlssTag             = "v310.6.0"
$DlssExpectedCommit  = "d1bef2006b41eefd9d44b0a05f123993f3acbf3c"
$DlssRepoUrl         = "https://github.com/NVIDIA/DLSS.git"
$DlssTargetDir       = Join-Path $RepoRoot "Third Parties\DLSS"
$DlssRuntimeSource   = Join-Path $DlssTargetDir "lib\Windows_x86_64\rel\nvngx_dlss.dll"
$DlssRuntimeDest     = Join-Path $RepoRoot "streamline\bin\x64\nvngx_dlss.dll"
$DlssBackupDir       = Join-Path $RepoRoot "streamline\bin\x64\backup_nvngx_dlss_before_script"
$DlssBackupPath      = Join-Path $DlssBackupDir "nvngx_dlss.dll"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "DLSS: git is required to clone pinned SDK tag $DlssTag."
    exit 1
}

if (Test-Path $DlssTargetDir) {
    Write-Host "[INFO] DLSS: removing existing directory: $DlssTargetDir"
    Remove-Item -Path $DlssTargetDir -Recurse -Force
}

Write-Host "[INFO] DLSS: cloning tag $DlssTag from $DlssRepoUrl ..."
$cloneCmd = "git clone --branch `"$DlssTag`" --depth 1 -- `"$DlssRepoUrl`" `"$DlssTargetDir`""
$oldErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
cmd /c $cloneCmd
$cloneExitCode = $LASTEXITCODE
$ErrorActionPreference = $oldErrorActionPreference
if ($cloneExitCode -ne 0) {
    Write-Error "DLSS: failed to clone repository tag $DlssTag."
    exit 1
}

$dlssHead = (& git -C "$DlssTargetDir" rev-parse HEAD 2>$null).Trim().ToLowerInvariant()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($dlssHead)) {
    Write-Error "DLSS: failed to determine cloned commit hash."
    exit 1
}
if ($dlssHead -ne $DlssExpectedCommit.ToLowerInvariant()) {
    Write-Error "DLSS: unexpected commit for $DlssTag. Expected: $DlssExpectedCommit  Got: $dlssHead"
    exit 1
}
Write-Host "[INFO] DLSS: pinned commit verified: $dlssHead"

if (-not (Test-Path $DlssRuntimeSource)) {
    Write-Error "DLSS: expected runtime DLL missing: $DlssRuntimeSource"
    exit 1
}

$runtimeDestDir = Split-Path -Parent $DlssRuntimeDest
New-Item -ItemType Directory -Path $runtimeDestDir -Force | Out-Null

if (Test-Path $DlssRuntimeDest) {
    Write-Host "[INFO] DLSS: backing up existing runtime DLL to: $DlssBackupPath"
    New-Item -ItemType Directory -Path $DlssBackupDir -Force | Out-Null
    Copy-Item -Path $DlssRuntimeDest -Destination $DlssBackupPath -Force
}

Write-Host "[INFO] DLSS: copying runtime DLL to streamline runtime folder..."
Copy-Item -Path $DlssRuntimeSource -Destination $DlssRuntimeDest -Force

$srcHash = (Get-FileHash -Path $DlssRuntimeSource -Algorithm SHA256).Hash.ToLowerInvariant()
$dstHash = (Get-FileHash -Path $DlssRuntimeDest -Algorithm SHA256).Hash.ToLowerInvariant()
if ($srcHash -ne $dstHash) {
    Write-Error "DLSS: runtime DLL hash mismatch after copy. Source: $srcHash  Destination: $dstHash"
    exit 1
}

$srcItem = Get-Item $DlssRuntimeSource
$dstItem = Get-Item $DlssRuntimeDest
$srcVer  = $srcItem.VersionInfo
$dstVer  = $dstItem.VersionInfo

Write-Host "[INFO] DLSS: source   fileVersion=$($srcVer.FileVersion) productVersion=$($srcVer.ProductVersion) sha256=$srcHash"
Write-Host "[INFO] DLSS: runtime  fileVersion=$($dstVer.FileVersion) productVersion=$($dstVer.ProductVersion) sha256=$dstHash"
Write-Host "[INFO] DLSS: done. SDK at: $DlssTargetDir"

if ($DlssOnly) {
    Write-Host "[INFO] DLSS-only mode enabled; skipping NRD/NRI downloads."
    Write-Host "[INFO] Third-party downloads finished."
    exit 0
}

# --- NRD ----------------------------------------------------------------
$NrdTag           = "v4.17.2"
$NrdDownloadUrl   = "https://github.com/NVIDIA-RTX/NRD/archive/refs/tags/$NrdTag.zip"
$NrdExpectedSha256 = "7A228DDF07658F581499A305D0F799BC7B31445014AB02931CFC3052660B7478"
$NrdTargetDir     = Join-Path $RepoRoot "Third Parties\NRD"
$StagingRoot      = Join-Path $env:TEMP "QuasimodoRTX-thirdparties-staging"
$NrdZipPath       = Join-Path $StagingRoot "nrd-$NrdTag.zip"
$NrdExtractDir    = Join-Path $StagingRoot "nrd-extract"

Write-Host "[INFO] NRD: preparing staging directory: $StagingRoot"
Remove-Item -Path $StagingRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $StagingRoot -Force | Out-Null
New-Item -ItemType Directory -Path $NrdExtractDir -Force | Out-Null

Write-Host "[INFO] NRD: downloading $NrdTag from GitHub..."
Invoke-WebRequest -Uri $NrdDownloadUrl -OutFile $NrdZipPath -UseBasicParsing

Write-Host "[INFO] NRD: verifying SHA256..."
$nrdHash = (Get-FileHash -Path $NrdZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($nrdHash -ne $NrdExpectedSha256.ToLowerInvariant()) {
    Write-Error "NRD SHA256 mismatch. Expected: $NrdExpectedSha256  Got: $nrdHash"
    exit 1
}
Write-Host "[INFO] NRD: SHA256 verified OK."

Write-Host "[INFO] NRD: extracting archive..."
Expand-Archive -Path $NrdZipPath -DestinationPath $NrdExtractDir -Force

$inner = Get-ChildItem -Path $NrdExtractDir -Directory | Select-Object -First 1
if (-not $inner) {
    Write-Error "NRD: unexpected zip layout (no top-level folder)."
    exit 1
}

if (Test-Path $NrdTargetDir) {
    Write-Host "[INFO] NRD: removing existing directory: $NrdTargetDir"
    Remove-Item -Path $NrdTargetDir -Recurse -Force
}

Write-Host "[INFO] NRD: installing to: $NrdTargetDir"
New-Item -ItemType Directory -Path (Split-Path -Parent $NrdTargetDir) -Force | Out-Null
Move-Item -Path $inner.FullName -Destination $NrdTargetDir

# NRI must live at Third Parties\NRD\NRI (see extern/CMakeLists.txt FETCHCONTENT_SOURCE_DIR_NRI).
# NRD's upstream CMake pulls NRI v178 for NRD v4.17.2; we vendor it there so CMake does not fetch.
$NriTag            = "v178"
$NriDownloadUrl    = "https://github.com/NVIDIA-RTX/NRI/archive/refs/tags/$NriTag.zip"
$NriExpectedSha256 = "08EDF187EA5C5D0C57B4D49F934ECF5299354DAFF8086FA7562959AF9AB3F153"
$NriTargetDir      = Join-Path $NrdTargetDir "NRI"
$NriZipPath        = Join-Path $StagingRoot "nri-$NriTag.zip"
$NriExtractDir     = Join-Path $StagingRoot "nri-extract"

Write-Host "[INFO] NRI: preparing extract directory: $NriExtractDir"
Remove-Item -Path $NriExtractDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $NriExtractDir -Force | Out-Null

Write-Host "[INFO] NRI: downloading $NriTag from GitHub..."
Invoke-WebRequest -Uri $NriDownloadUrl -OutFile $NriZipPath -UseBasicParsing

Write-Host "[INFO] NRI: verifying SHA256..."
$nriHash = (Get-FileHash -Path $NriZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($nriHash -ne $NriExpectedSha256.ToLowerInvariant()) {
    Write-Error "NRI SHA256 mismatch. Expected: $NriExpectedSha256  Got: $nriHash"
    exit 1
}
Write-Host "[INFO] NRI: SHA256 verified OK."

Write-Host "[INFO] NRI: extracting archive..."
Expand-Archive -Path $NriZipPath -DestinationPath $NriExtractDir -Force

$nriInner = Get-ChildItem -Path $NriExtractDir -Directory | Select-Object -First 1
if (-not $nriInner) {
    Write-Error "NRI: unexpected zip layout (no top-level folder)."
    exit 1
}

if (Test-Path $NriTargetDir) {
    Write-Host "[INFO] NRI: removing existing directory: $NriTargetDir"
    Remove-Item -Path $NriTargetDir -Recurse -Force
}

Write-Host "[INFO] NRI: installing to: $NriTargetDir"
Move-Item -Path $nriInner.FullName -Destination $NriTargetDir

Write-Host "[INFO] NRD/NRI: cleaning staging directory..."
Remove-Item -Path $StagingRoot -Recurse -Force

Write-Host "[INFO] NRD: done. SDK at: $NrdTargetDir"
Write-Host "[INFO] NRI: done. SDK at: $NriTargetDir"

# --- JoltPhysics (official upstream clone only — not built by this script; ignored by git)
$JoltRepoUrl   = "https://github.com/jrouwe/JoltPhysics.git"
$JoltTargetDir = Join-Path $RepoRoot "Third Parties\JoltPhysics"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "JoltPhysics: git is required to clone or verify the repository."
    exit 1
}

if (Test-Path $JoltTargetDir) {
    $gitMarker = Join-Path $JoltTargetDir ".git"
    if (-not (Test-Path $gitMarker)) {
        Write-Error "JoltPhysics: path exists but is not a git clone (missing .git): $JoltTargetDir"
        exit 1
    }
    $remoteLines = @(git -C $JoltTargetDir remote -v 2>$null)
    if ($LASTEXITCODE -ne 0) {
        Write-Error "JoltPhysics: failed to read git remotes for $JoltTargetDir"
        exit 1
    }
    $remoteBlob = $remoteLines -join "`n"
    if ($remoteBlob -notmatch 'jrouwe/JoltPhysics') {
        Write-Error "JoltPhysics: remotes must reference jrouwe/JoltPhysics. Got:`n$remoteBlob"
        exit 1
    }
    $joltShort = (git -C $JoltTargetDir rev-parse --short HEAD 2>$null).Trim()
    if ([string]::IsNullOrWhiteSpace($joltShort)) {
        Write-Error "JoltPhysics: could not read HEAD commit for $JoltTargetDir"
        exit 1
    }
    Write-Host "[INFO] JoltPhysics: existing clone OK at $JoltTargetDir (commit $joltShort); no automatic pull."
} else {
    Write-Host "[INFO] JoltPhysics: cloning $JoltRepoUrl -> $JoltTargetDir"
    $parentJolt = Split-Path -Parent $JoltTargetDir
    if (-not (Test-Path $parentJolt)) {
        New-Item -ItemType Directory -Path $parentJolt -Force | Out-Null
    }
    & git clone $JoltRepoUrl $JoltTargetDir
    if ($LASTEXITCODE -ne 0) {
        Write-Error "JoltPhysics: git clone failed."
        exit 1
    }
    $joltShortNew = (git -C $JoltTargetDir rev-parse --short HEAD 2>$null).Trim()
    Write-Host "[INFO] JoltPhysics: cloned OK (commit $joltShortNew)"
}

Write-Host "[INFO] All third-party downloads finished."
