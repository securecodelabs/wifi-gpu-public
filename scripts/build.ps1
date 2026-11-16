param([string]$Compiler)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& "$PSScriptRoot\verify-shaders.ps1"
if (-not $Compiler) {
    $zigCommand = Get-Command zig -ErrorAction SilentlyContinue
    if ($zigCommand) { $Compiler = $zigCommand.Source }
    else { throw 'Zig not found on PATH. Pass -Compiler, or build with CMake and Visual Studio.' }
}
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Zig not found. Pass -Compiler C:\path\to\zig.exe, or use CMake with Visual Studio.' }
$output = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$versionMatch = [regex]::Match((Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(wifi_gpu VERSION ([0-9]+\.[0-9]+\.[0-9]+) ')
if (-not $versionMatch.Success) { throw 'Cannot read wifi-gpu version from CMakeLists.txt' }
$versionHeader = Join-Path $output 'build-version.hpp'
[System.IO.File]::WriteAllText($versionHeader, '#define WIFI_GPU_VERSION "' + $versionMatch.Groups[1].Value + '"' + "`n")
$oldGlobal = $env:ZIG_GLOBAL_CACHE_DIR
$oldLocal = $env:ZIG_LOCAL_CACHE_DIR
try {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $output 'zig-global-cache'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $output 'zig-local-cache'
    $sources = @('src\main.cpp', 'src\gpu.cpp', 'src\opencl_gpu.cpp', 'src\hip_gpu.cpp', 'src\vulkan_gpu.cpp', 'src\reference.cpp', 'src\cpu.cpp', 'src\crypto.cpp', 'src\crypto_common.cpp', 'src\files.cpp', 'src\hc22000.cpp', 'src\candidates.cpp', 'src\recovery.cpp', 'src\recovery_tests.cpp') | ForEach-Object { Join-Path $root $_ }
    & $Compiler c++ -std=c++17 -O3 -Wall -Wextra -Werror -include $versionHeader -I (Join-Path $root 'third_party\OpenCL-Headers') -I (Join-Path $root 'third_party\Vulkan-Headers') @sources -lbcrypt -lshell32 -o (Join-Path $output 'wifi-gpu.exe')
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
    New-Item -ItemType Directory -Force -Path (Join-Path $output 'kernels') | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'kernels\pbkdf2.cl') -Destination (Join-Path $output 'kernels\pbkdf2.cl')
    Copy-Item -LiteralPath (Join-Path $root 'kernels\pbkdf2.spv') -Destination (Join-Path $output 'kernels\pbkdf2.spv')
    Write-Host "Built $output\wifi-gpu.exe"
} finally {
    $env:ZIG_GLOBAL_CACHE_DIR = $oldGlobal
    $env:ZIG_LOCAL_CACHE_DIR = $oldLocal
}
