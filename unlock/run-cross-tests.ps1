# Run the cross-hosted test binaries built by run-cross-tests.sh (WSL clang +
# MSVC CRT). WSL interop is not required. Mirrors test-all.ps1's no-fixture
# jobs plus the GA107M dmem regression test.
param(
  # Override with your own paths (default assumes the session layout used
  # during development; fixture tests are skipped when files are absent).
  [string]$PromFixture = '',
  [string]$AssetsDir = ''
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$Out = 'build\tests-cross'
if (!(Test-Path "$Out\sanity.exe")) { throw "Cross-built tests missing; run run-cross-tests.sh in WSL first" }

$exes = @('sanity', 'port', 'write-lock', 'master-attributes', 'runtime',
          'profile-lock-preserve', 'profile-lock-full', 'vbios-dmem')
$results = @()
foreach ($name in $exes) {
  $exe = Join-Path $Out "$name.exe"
  if (!(Test-Path $exe)) { throw "Missing test binary: $exe" }
  Write-Host "== run $name =="
  # Keep each test's PASS line visible; capture exit code.
  & $exe
  $code = $LASTEXITCODE
  $results += [ordered]@{ Name = $name; ExitCode = $code }
  if ($code -ne 0) { throw "Test failed: $name (exit $code)" }
}

# Fixture tests: full-transaction mock + sha256 against the REAL captured PROM.
if ((Test-Path $PromFixture) -and (Test-Path $AssetsDir)) {
  $core = Join-Path $Out 'core-transaction.exe'
  $sha  = Join-Path $Out 'sha-realprom.exe'
  if (!(Test-Path $core) -or !(Test-Path $sha)) { throw "Fixture test binaries missing; rerun run-cross-tests.sh" }
  Write-Host "== run core-transaction (real PROM + pinned assets) =="
  & $core $PromFixture $AssetsDir
  $code = $LASTEXITCODE
  $results += [ordered]@{ Name = 'core-transaction'; ExitCode = $code }
  if ($code -ne 0) { throw "Test failed: core-transaction (exit $code)" }
  Write-Host "== run sha-realprom (real GSP asset + real PROM vs this card's pin) =="
  & $sha (Join-Path $AssetsDir 'gsp_ga10x.bin') $PromFixture (Select-String -Path (Join-Path $PSScriptRoot 'port.h') -Pattern 'GU_CARD_PROFILE_PROM_SHA256\s+"([0-9a-f]{64})"').Matches[0].Groups[1].Value
  $code = $LASTEXITCODE
  $results += [ordered]@{ Name = 'sha-realprom'; ExitCode = $code }
  if ($code -ne 0) { throw "Test failed: sha-realprom (exit $code)" }
} else {
  Write-Warning "PROM fixture or assets dir missing; skipped fixture tests"
}

Write-Host "== build-script parameter rejections =="
$rejections = 0
$previousEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
  foreach ($script in @('build.ps1', 'test-all.ps1')) {
    foreach ($args in @(
        @('-ExperimentalUnlock'),
        @('-ExperimentalUnlock', '-StockProfile', 'unknown'),
        @('-StockProfile', 'original03100062'),
        @('-StockProfile', 'observed03000062'),
        @('-StockProfile', 'entryLatchedSs1Preserve'),
        @('-StockProfile', 'entryLatchedSs1Full'),
        @('-ExperimentalUnlock', '-StockProfile', '')
      )) {
      # -ExperimentalUnlock without a pinned profile must fail closed (#error),
      # unknown/removed profiles must be rejected by ValidateSet, and
      # -StockProfile without -ExperimentalUnlock must be rejected by the scripts.
      # stderr is redirected to $null; under EAP Continue the child's
      # NativeCommandError records do not terminate this script.
      & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $script) @args *> $null
      if ($LASTEXITCODE -eq 0) { throw "Expected rejection: $script $($args -join ' ')" }
      $rejections++
    }
  }
} finally { $ErrorActionPreference = $previousEap }
Write-Host "PASS all parameter rejections ($rejections)"
[ordered]@{ AllPassed = $true; Tests = $results; ParameterRejections = $rejections } |
  ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$Out\test-results-cross.json"
Write-Host 'ALL CROSS-HOST TESTS PASSED'
