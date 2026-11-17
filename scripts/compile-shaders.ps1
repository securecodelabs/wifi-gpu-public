param([string]$Glslang)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Glslang) {
    $command = Get-Command glslang -ErrorAction SilentlyContinue
    if (-not $command) { $command = Get-Command glslangValidator -ErrorAction SilentlyContinue }
    if ($command) { $Glslang = $command.Source }
    elseif ($env:VULKAN_SDK) { $Glslang = Join-Path $env:VULKAN_SDK 'Bin\glslangValidator.exe' }
    else { throw 'Pass -Glslang C:\path\to\glslang.exe (Khronos glslang or Vulkan SDK).' }
}
if (-not (Test-Path -LiteralPath $Glslang)) { throw "Shader compiler not found: $Glslang" }
$source = Join-Path $root 'kernels\pbkdf2.comp'
$binary = Join-Path $root 'kernels\pbkdf2.spv'
$temporary = "$binary.tmp.$PID"
try {
    & $Glslang -V --target-env vulkan1.0 -Os --spirv-val --source-entrypoint main -e wifi_pbkdf2_v4 -o $temporary $source
    if ($LASTEXITCODE -ne 0) { throw 'Shader compilation/validation failed; existing binary retained.' }
    Move-Item -LiteralPath $temporary -Destination $binary -Force
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
function Get-Sha256([string]$Path) {
    $hash = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose() }
}
$lines = foreach ($file in @('pbkdf2.comp','pbkdf2.spv')) {
    "$(Get-Sha256 (Join-Path $root "kernels\$file"))  $file"
}
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $root 'kernels\checksums.sha256'), ($lines -join "`n")+"`n", $utf8)
$version = (& $Glslang --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot query shader compiler version.' }
$manifest = [ordered]@{
    compiler = $version
    compiler_sha256 = Get-Sha256 $Glslang
    arguments = @('-V','--target-env','vulkan1.0','-Os','--spirv-val','--source-entrypoint','main','-e','wifi_pbkdf2_v4')
    target = 'Vulkan 1.0 / SPIR-V 1.0'
}
[IO.File]::WriteAllText((Join-Path $root 'kernels\compiler.json'), ($manifest | ConvertTo-Json -Depth 5)+"`n", $utf8)
& "$PSScriptRoot\verify-shaders.ps1"
Write-Host 'Compiled and validated Vulkan shader; source/binary checksums refreshed.'
