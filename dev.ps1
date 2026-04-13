# dev.ps1 -- Q2RTX developer automation dispatcher
# Usage: .\dev.ps1 <command>
# Commands: configure, build, rebuild, clean, clean-build, run, shaders, doctor, submodules, thirdparties

param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet("configure", "build", "rebuild", "clean", "clean-build", "run", "shaders", "doctor", "submodules", "thirdparties")]
    [string]$Command
)

$scriptDir = Join-Path $PSScriptRoot "scripts\dev"
$scriptMap = @{
    configure   = "configure.ps1"
    build       = "build.ps1"
    rebuild     = "rebuild.ps1"
    clean       = "clean.ps1"
    "clean-build" = "clean-build.ps1"
    run         = "run.ps1"
    shaders     = "shaders.ps1"
    doctor      = "doctor.ps1"
    submodules  = "submodules.ps1"
    thirdparties = "thirdparties.ps1"
}

$scriptFile = Join-Path $scriptDir $scriptMap[$Command]
if (-not (Test-Path $scriptFile)) {
    Write-Error "Script not found: $scriptFile"
    exit 1
}

& $scriptFile
exit $LASTEXITCODE
