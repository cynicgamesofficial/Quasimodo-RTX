# _common.ps1 -- Shared utilities for Q2RTX dev scripts
# All paths use repo root as base. Safe to call from any working directory.
# Requires: PowerShell 5.1+

$ErrorActionPreference = "Stop"
$script:RepoRoot = $null

function Get-RepoRoot {
    if ($script:RepoRoot) { return $script:RepoRoot }
    $scriptDir = Split-Path $PSScriptRoot -Parent
    try {
        $root = (git -C $scriptDir rev-parse --show-toplevel 2>$null)
        if ($root) {
            $script:RepoRoot = $root.Trim()
            return $script:RepoRoot
        }
    } catch { }
    $dir = (Resolve-Path $scriptDir).Path
    while ($dir) {
        $cmake = Join-Path $dir "CMakeLists.txt"
        if (Test-Path $cmake) {
            $content = Get-Content $cmake -Raw -ErrorAction SilentlyContinue
            if ($content -match 'project\s*\(\s*quake2-RTX') {
                $script:RepoRoot = $dir
                return $script:RepoRoot
            }
        }
        $parent = Split-Path $dir -Parent
        if (-not $parent -or $parent -eq $dir) { break }
        $dir = $parent
    }
    Write-Error "Could not determine Q2RTX repository root"
}

function Get-BuildDir { Join-Path (Get-RepoRoot) "build" }
function Get-ExecutablePath { Join-Path (Get-RepoRoot) "q2rtx.exe" }
function Get-BaseQ2Dir { Join-Path (Get-RepoRoot) "baseq2" }
function Get-ShaderOutputDir { Join-Path (Get-RepoRoot) "baseq2\shader_vkpt" }

function Write-DevLog {
    param([string]$Message, [string]$Level = "INFO")
    $prefix = switch ($Level) {
        "ERR"   { "[ERR] " }
        "WARN"  { "[WARN] " }
        default { "[INFO] " }
    }
    Write-Host "$prefix$Message"
}

function Test-VulkanSDK {
    $vk = $env:VULKAN_SDK
    if (-not $vk -or -not (Test-Path $vk)) {
        Write-DevLog "VULKAN_SDK is not set or path does not exist" "ERR"
        Write-DevLog "Install Vulkan SDK from https://www.lunarg.com/vulkan-sdk/" "ERR"
        return $false
    }
    return $true
}
