#Requires -Version 5.1
<#
.SYNOPSIS
  Build a one-file Windows GUI launcher (launcher.exe) at the repository root.

.NOTES
  Run manually after installing PyInstaller in your environment:
    pip install pyinstaller
  Does not run automatically. Does not commit binaries.

  Example (from repo root):
    powershell -ExecutionPolicy Bypass -File tools/quasimodo_wizard/package_launcher.ps1
#>
$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$MainPy = Join-Path $PSScriptRoot "main.py"
$Icon = Join-Path $PSScriptRoot "assets\icons\quasimodo_wizard.ico"
if (-not (Test-Path $MainPy)) { throw "Missing $MainPy" }

$iconArg = @()
if (Test-Path $Icon) {
  $iconArg = @("--icon", $Icon)
} else {
  Write-Warning "Icon not found at $Icon — building without custom icon."
}

Push-Location $RepoRoot
try {
  pyinstaller --noconfirm --clean --onefile --windowed --name launcher @iconArg $MainPy
  $built = Join-Path $RepoRoot "dist\launcher.exe"
  $rootExe = Join-Path $RepoRoot "launcher.exe"
  if (Test-Path $built) {
    Copy-Item -Force $built $rootExe
    Write-Host "Copied to $rootExe"
  } else {
    Write-Warning "Expected output not found: $built"
  }
}
finally {
  Pop-Location
}
