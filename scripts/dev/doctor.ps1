# doctor.ps1 -- Report build and environment status
. "$PSScriptRoot\_common.ps1"

$root = Get-RepoRoot
$buildDir = Get-BuildDir
$exe = Get-ExecutablePath
$baseq2 = Get-BaseQ2Dir

function Test-Check { param($Name, $Ok, $Detail)
    $status = if ($Ok) { "OK" } else { "MISSING" }
    $color = if ($Ok) { "Green" } else { "Red" }
    Write-Host ("  {0,-20} [{1}] {2}" -f $Name, $status, $Detail) -ForegroundColor $color
}

Write-Host "`n=== Q2RTX Doctor ===" -ForegroundColor Cyan
Write-Host "Repo root: $root`n"

# Git
$gitOk = $null -ne (Get-Command git -ErrorAction SilentlyContinue)
$gitVer = ""
if ($gitOk) { $gitVer = (git --version 2>$null) }
Test-Check "Git" $gitOk $gitVer

# CMake
$cmakeOk = $null -ne (Get-Command cmake -ErrorAction SilentlyContinue)
$cmakeVer = ""
if ($cmakeOk) { $cmakeVer = (cmake --version 2>$null | Select-Object -First 1) }
Test-Check "CMake" $cmakeOk $cmakeVer

# Visual Studio (MSBuild)
$msbuildOk = $null -ne (Get-Command msbuild -ErrorAction SilentlyContinue)
if (-not $msbuildOk) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        $msbuildOk = [bool]$vsPath
    }
}
Test-Check "Visual Studio / MSBuild" $msbuildOk ""

# Vulkan SDK
$vkOk = $false
$vkPath = $env:VULKAN_SDK
if ($vkPath -and (Test-Path $vkPath)) {
    $glslang = Join-Path $vkPath "Bin\glslangValidator.exe"
    $vkOk = Test-Path $glslang
}
$vkDetail = if ($vkPath) { $vkPath } else { "VULKAN_SDK not set" }
Test-Check "Vulkan SDK" $vkOk $vkDetail

# Submodules
$submodOk = $true
$submods = @("extern\zlib", "extern\SDL2", "extern\curl", "extern\openal-soft", "extern\Vulkan-Headers", "extern\glslang", "extern\stb", "extern\tinyobjloader-c")
foreach ($s in $submods) {
    $p = Join-Path $root $s
    $hasCmake = Test-Path (Join-Path $p "CMakeLists.txt")
    if (-not $hasCmake) {
        $hasFiles = (Test-Path $p) -and (Get-ChildItem $p -ErrorAction SilentlyContinue)
        if (-not $hasFiles) { $submodOk = $false; break }
    }
}
Test-Check "Submodules" $submodOk "Run .\dev.ps1 submodules"

# Build folder
$buildExists = Test-Path $buildDir
$buildConfigured = $buildExists -and (Test-Path (Join-Path $buildDir "CMakeCache.txt"))
Test-Check "Build folder" $buildExists ""
Test-Check "  (configured)" $buildConfigured ""

# Executable
$exeExists = Test-Path $exe
$exeDetail = if ($exeExists) { $exe } else { "" }
Test-Check "q2rtx.exe" $exeExists $exeDetail

# Assets
$baseExists = Test-Path $baseq2
$hasPkz = Test-Path (Join-Path $baseq2 "q2rtx_media.pkz")
$hasPak = $baseExists -and (Get-ChildItem -Path $baseq2 -Filter "pak*.pak" -ErrorAction SilentlyContinue)
$assetsOk = $baseExists -and ($hasPkz -or $hasPak)
$assetsDetail = if ($assetsOk) { "present" } else { "Add q2rtx_media.pkz or pak*.pak" }
Test-Check "baseq2/ assets" $assetsOk $assetsDetail

Write-Host ""
