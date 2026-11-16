param([switch]$CpuOnly, [string]$Compiler, [string]$Python, [ValidateSet("opencl","vulkan")][string]$Backend="vulkan")
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot\build.ps1" -Compiler $Compiler
$exe = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\wifi-gpu.exe'
$arguments = @('selftest')
if ($CpuOnly) { $arguments += '--cpu-only' } else { $arguments += @('--backend', $Backend) }
& $exe @arguments
if ($LASTEXITCODE -ne 0) { throw "Self-test failed: $LASTEXITCODE" }
$invalid = @(
    @('benchmark', '--batch', '0'), @('benchmark', '--batch', '4097'),
    @('benchmark', '--chunk', '0'), @('benchmark', '--chunk', '4294967297'),
    @('benchmark', '--repeats', '0'), @('benchmark', '--group', '0'),
    @('benchmark', '--device', '-1'), @('benchmark', '--repeats', '2oops'),
    @('benchmark', '--batch'), @('benchmark', '--unknown', '1'),
    @('benchmark', '--batch', '2', '--batch', '4'),
    @('devices', '--device', '0'), @('selftest', '--cpu-only', '--device', '0'),
    @('recover'), @('--version', '--junk'),
    @('devices','--backend','unknown'), @('selftest','--cpu-only','--backend','vulkan'),
    @('devices','--backend'), @('devices','--backend','vulkan','--backend','opencl'),
    @('benchmark','--threads','2'), @('benchmark','--cpu','--threads','0'),
    @('benchmark','--cpu','--threads','65'), @('benchmark','--cpu','--group','64'),
    @('benchmark','--cpu','--backend','vulkan'), @('benchmark','--cpu','--chunk','128')
)
foreach ($case in $invalid) {
    # Native stderr behavior differs between PowerShell 5 and 7: collect using a process.
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $exe
    $psi.Arguments = $case -join ' '
    $psi.UseShellExecute = $false
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardOutput = $true
    $process = [System.Diagnostics.Process]::Start($psi)
    $stderr = $process.StandardError.ReadToEnd()
    $stdout = $process.StandardOutput.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 1 -or -not $stderr.StartsWith('error:')) {
        throw "Bad CLI rejection for '$($case -join ' ')': $stdout $stderr"
    }
    $process.Dispose()
}
Write-Host "PASS: $($invalid.Count) CLI rejection cases"
if (-not $Python) {
    $command = Get-Command python -ErrorAction SilentlyContinue
    if (-not $command) { throw 'Python 3 is required for integration tests; pass -Python.' }
    $Python = $command.Source
}
$integration = @((Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\test_recovery.py'), '--exe', $exe)
if ($CpuOnly) { $integration += '--cpu-only' } else { $integration += @('--backend', $Backend) }
$logRoot = Join-Path (Split-Path -Parent $PSScriptRoot) ('build\test-logs\' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
$runner = Join-Path $PSScriptRoot 'run_checked.py'
Write-Host "Test diagnostics: $logRoot"
& $Python $runner --logs $logRoot --name integration -- $Python @integration
if ($LASTEXITCODE -ne 0) { throw 'Recovery integration tests failed.' }
$snapshotBackend = if ($CpuOnly) { 'cpu' } else { $Backend }
& $Python $runner --logs $logRoot --name snapshots -- $Python (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\test_snapshot_contention.py') --exe $exe --backend $snapshotBackend
if ($LASTEXITCODE -ne 0) { throw 'Snapshot contention regression tests failed.' }
if ($CpuOnly) {
    & $Python $runner --logs $logRoot --name cpu-cancellation -- $Python (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\test_cpu_cancellation.py') --exe $exe --iterations 30
    if ($LASTEXITCODE -ne 0) { throw 'CPU cancellation stress tests failed.' }
}
& $Python $runner --logs $logRoot --name diagnostics -- $Python (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\test_runner.py')
if ($LASTEXITCODE -ne 0) { throw 'Test diagnostic capture regression failed.' }
& $Python $runner --logs $logRoot --name shader-guards -- $Python (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\test_shader_build.py')
if ($LASTEXITCODE -ne 0) { throw 'Shader build guard tests failed.' }
& $Python $runner --logs $logRoot --name generated-sha1 -- $Python (Join-Path $PSScriptRoot 'generate-sha1.py') --check
if ($LASTEXITCODE -ne 0) { throw 'Generated SHA-1 check failed.' }
