# configure.ps1 -- Configure Q2RTX build (Visual Studio 2022 x64, RelWithDebInfo)
. "$PSScriptRoot\_common.ps1"

$root = Get-RepoRoot
$buildDir = Get-BuildDir

Write-DevLog "Configuring Q2RTX at $root"

if (-not (Test-VulkanSDK)) { exit 1 }

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    Write-DevLog "Created build directory: $buildDir"
}

Push-Location $root
try {
    & cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo
    if ($LASTEXITCODE -ne 0) { exit 1 }
    Write-DevLog "Configuration complete. Build with: .\dev.ps1 build"
} finally { Pop-Location }
