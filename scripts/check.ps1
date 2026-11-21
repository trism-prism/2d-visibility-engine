#requires -Version 7
# scripts/check.ps1: the deterministic "done" gate for humans, agents, Orca automations and CI.
#   pwsh -NoProfile -File scripts/check.ps1 -Tier fast|quick|full
#   fast: configure (release)      quick: + build, ctest (vis_tests, app_script, bench_quick)      full: + asan preset configure, build, ctest
# Exit code 0 = pass. Keep each step a real command; no prose checks.
# Every toolchain call goes through scripts/dev.ps1 (MSVC dev env + bundled CMake/Ninja), run as a child pwsh because it calls exit.
param([ValidateSet('fast', 'quick', 'full')][string]$Tier = 'quick')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
$failed = @()
function Step([string]$Name, [string[]]$Tiers, [scriptblock]$Run) {
    if ($Tiers -notcontains $script:Tier) { return }
    Write-Host "-> $Name" -ForegroundColor Cyan
    try { & $Run; if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "exit $LASTEXITCODE" } }
    catch { Write-Host "   FAILED: $Name ($_)" -ForegroundColor Red; $script:failed += $Name }
}
function Dev { pwsh -NoProfile -File (Join-Path $root 'scripts/dev.ps1') @args }
try {
    Step 'configure (release)' @('fast', 'quick', 'full') { Dev cmake --preset release }
    Step 'build (release)' @('quick', 'full') { Dev cmake --build --preset release }
    Step 'ctest (release)' @('quick', 'full') { Dev ctest --preset release }
    Step 'configure (asan)' @('full') { Dev cmake --preset asan }
    Step 'build (asan)' @('full') { Dev cmake --build --preset asan }
    Step 'ctest (asan)' @('full') { Dev ctest --preset asan }
} finally { Pop-Location }
if ($failed.Count) { Write-Host "check ($Tier): FAILED -> $($failed -join ', ')" -ForegroundColor Red; exit 1 }
Write-Host "check ($Tier): OK" -ForegroundColor Green
