# shaders.ps1 -- Compile Vulkan shaders via CMake shaders target
. "$PSScriptRoot\_common.ps1"

$buildDir = Get-BuildDir

if (-not (Test-Path $buildDir)) {
    Write-DevLog "Build directory does not exist. Run: .\dev.ps1 configure" "ERR"
    exit 1
}

Write-DevLog "Compiling shaders..."
Push-Location (Get-RepoRoot)
try {
    & cmake --build build --target shaders --config RelWithDebInfo -- /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { exit 1 }
    $shaderDir = Get-ShaderOutputDir
    if (Test-Path $shaderDir) {
        $count = (Get-ChildItem -Path $shaderDir -Filter "*.spv" -ErrorAction SilentlyContinue).Count
        Write-DevLog "Shaders output: $shaderDir ($count .spv files)"
    }
} finally { Pop-Location }
