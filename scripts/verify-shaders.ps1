$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lines = @(Get-Content -LiteralPath (Join-Path $root 'kernels\checksums.sha256'))
if ($lines.Count -ne 2) { throw 'Invalid shader checksum manifest.' }
$expected = @('pbkdf2.comp','pbkdf2.spv')
for ($i=0; $i -lt 2; $i++) {
    if ($lines[$i] -notmatch '^([a-f0-9]{64})  (pbkdf2\.(comp|spv))$' -or $Matches[2] -ne $expected[$i]) {
        throw 'Invalid shader checksum entry.'
    }
    $digest = $Matches[1]
    $path = Join-Path $root "kernels\$($expected[$i])"
    $hash = [Security.Cryptography.SHA256]::Create()
    try { $actual = ([BitConverter]::ToString($hash.ComputeHash([IO.File]::ReadAllBytes($path)))).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose() }
    if ($actual -ne $digest) { throw "Shader source/binary mismatch: $path. Run scripts\compile-shaders.ps1 before building." }
}
