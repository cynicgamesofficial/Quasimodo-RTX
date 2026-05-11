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
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ToolRoot = $PSScriptRoot
$MainPy = Join-Path $ToolRoot "main.py"
$Icon = Join-Path $ToolRoot "assets\icons\quasimodo_wizard.ico"
if (-not (Test-Path $MainPy)) { throw "Missing $MainPy" }

$iconArg = @()
if (Test-Path $Icon) {
  $iconArg = @("--icon", $Icon)
} else {
  Write-Warning "Icon not found at $Icon - building without custom icon."
}

# PyInstaller --add-data uses "source;dest_in_bundle" on Windows (semicolon).
$PkgDir = Join-Path $ToolRoot "quasimodo_wizard"
$PresetsDir = Join-Path $ToolRoot "presets"
$AssetsDir = Join-Path $ToolRoot "assets"
$addData = @(
  "--add-data", "$PkgDir;quasimodo_wizard",
  "--add-data", "$PresetsDir;presets",
  "--add-data", "$AssetsDir;assets"
)

$mainRel = "tools\quasimodo_wizard\main.py"

Push-Location $RepoRoot
try {
  # --windowed: no console window (GUI-only). One-file exe does not require python.exe on PATH.
  & python -m PyInstaller --noconfirm --clean --onefile --windowed --name launcher @iconArg @addData $mainRel
  $built = Join-Path $RepoRoot "dist\launcher.exe"
  $rootExe = Join-Path $RepoRoot "launcher.exe"
  if (Test-Path $built) {
    Copy-Item -LiteralPath $built -Destination $rootExe -Force
    Write-Host "Copied to $rootExe"
  } else {
    Write-Warning "Expected output not found: $built"
  }
}
finally {
  Pop-Location
}
