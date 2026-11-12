# Runs any command inside the MSVC x64 developer environment (VS Build Tools 2022),
# with the bundled CMake and Ninja on PATH. Nothing is installed or changed system-wide, and the
# calling shell's environment is restored afterwards (repeated calls do not pile up PATH entries).
#
#   .\scripts\dev.ps1 cmake --preset release
#   .\scripts\dev.ps1 cmake --build --preset release
#   .\scripts\dev.ps1 ctest --preset release
#   .\scripts\dev.ps1 cmake -S . -B build\core-only -G Ninja -DVIS_BUILD_APP=OFF
#
# All arguments are forwarded verbatim; the exit code of the command is returned.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vs = if ($env:VIS_VS_PATH) { $env:VIS_VS_PATH } else { 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools' }
$vsdev = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $vsdev)) { Write-Error "VsDevCmd.bat not found: $vsdev (set VIS_VS_PATH)"; exit 2 }
if ($args.Count -eq 0) { Write-Error 'usage: dev.ps1 <command> [args...]'; exit 2 }

$saved = [Environment]::GetEnvironmentVariables('Process')
$code = 1
try {
    # Import the developer environment into this process only.
    $envDump = cmd /c "`"$vsdev`" -arch=x64 -host_arch=x64 >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd.bat failed' }
    foreach ($line in $envDump) {
        $i = $line.IndexOf('=')
        if ($i -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $i), $line.Substring($i + 1), 'Process') }
    }
    $cmakeBin = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
    $env:PATH = "$cmakeBin\CMake\bin;$cmakeBin\Ninja;$env:PATH"

    Push-Location -LiteralPath $root
    try {
        $exe = $args[0]
        $rest = @(); if ($args.Count -gt 1) { $rest = $args[1..($args.Count - 1)] }
        & $exe @rest
        $code = $LASTEXITCODE
    } finally { Pop-Location }
} finally {
    foreach ($name in @([Environment]::GetEnvironmentVariables('Process').Keys)) {
        if (-not $saved.Contains($name)) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    }
    foreach ($name in @($saved.Keys)) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
exit $code
