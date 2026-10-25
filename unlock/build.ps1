# Modified 2026-10-19/20 for Windows GPU UNLOCK; changes documented in CHANGES.md.
param(
 [string]$Clang='clang',
 [string]$LldLink='lld-link',
 [string]$Python='python',
 [switch]$ExperimentalUnlock,
 [ValidateSet('entryLatchedSs1Preserve','entryLatchedSs1Full')][string]$StockProfile='entryLatchedSs1Preserve'
)
$ErrorActionPreference='Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
 if(!(Test-Path -LiteralPath $PSScriptRoot)){throw 'Missing output parent'}
 if($ExperimentalUnlock -and !$PSBoundParameters.ContainsKey('StockProfile')){throw '-StockProfile is required with -ExperimentalUnlock'}
 if(!$ExperimentalUnlock -and $PSBoundParameters.ContainsKey('StockProfile')){throw '-StockProfile is only valid with -ExperimentalUnlock'}
 $out=if($ExperimentalUnlock){"build/experimental/$StockProfile"}else{'build'}
 $name=if($ExperimentalUnlock){"GA107M-EXPERIMENTAL-$StockProfile"}else{'GA107M-PREFLIGHT'}
 $defines=@()
  if($ExperimentalUnlock){$defines+= '-DGU_EXPERIMENTAL_3090';$defines+=$(switch($StockProfile){'entryLatchedSs1Preserve'{'-DGU_ENTRY_LATCHED_SS1_PRESERVE'} 'entryLatchedSs1Full'{'-DGU_ENTRY_LATCHED_SS1_FULL'}})}
 New-Item -ItemType Directory -Path $out -Force | Out-Null
 $sources=@('app','app_io','ownership','format','runtime','sha256','checks','port','compat',
  'vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval')
 $objects=@()
 foreach($source in $sources) {
  $object="$out/"+($source -replace '/','_')+'.obj'
  & $Clang -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -Wno-unused-parameter -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x25A010deU @defines -I compat -I ../deps/edk2/MdePkg/Include -I ../deps/edk2/MdePkg/Include/X64 -c "$source.c" -o $object
  if($LASTEXITCODE -ne 0){throw "Compile failed: $source"};$objects+=$object
 }
 & $LldLink /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf "/map:$out/$name.map" "/out:$out/$name.EFI" @objects
 if($LASTEXITCODE -ne 0){throw 'EFI link failed'}
 & $Python verify_pe.py "$out/$name.EFI" "$out/$name.map" --output "$out/pe-verification.json"
 if($LASTEXITCODE -ne 0){throw 'PE verification failed'}
 [ordered]@{BuiltUtc=[DateTime]::UtcNow.ToString('o');TargetPci='25A010DE';Subsystem='88E5103C';
  Mode=$(if($ExperimentalUnlock){'experimental-exact-card-manual-unlock'}else{'diagnostic-inventory-preflight-only'});
  CanArm=[bool]$ExperimentalUnlock;Defines=$defines;ManualConfirmation='U once; Esc or 30-second timeout cancels';
  StockProfile=$(if($ExperimentalUnlock){$StockProfile}else{$null});ColdChipExpected='from GU_EXPECTED_CHIP (pinned from preflight REPORT)';ColdSS0Expected=$(if($StockProfile -like 'entryLatchedSs1*'){'first fully validated cold entry; immutable'}elseif($ExperimentalUnlock){'n/a - entry latched profiles only'}else{'unknown'});
  ColdSS1Expected=$(if($StockProfile -like 'entryLatchedSs1*'){'first fully validated cold entry <= 0xf; immutable'}else{'unknown'});
  SS0Target='0x88888888';SS1Target=$(if($StockProfile -eq 'entryLatchedSs1Preserve'){'preserve entry; no SS1 writes'}elseif($StockProfile -eq 'entryLatchedSs1Full'){'0x8; one guarded write'}elseif($ExperimentalUnlock){'0x8'}else{'no writes'});
  OriginalBaselineCapture='GA107M-PREFLIGHT/RUN00/REPORT.TXT';
  StockProfileSource=$(if($StockProfile -like 'entryLatchedSs1*'){'Explicit immutable validated entry latch; no fixed stock selector assumption'}else{'preflight has no selected stock profile'});
  PromSha256=$(if($ExperimentalUnlock){'pinned in port.h GU_CARD_PROFILE_PROM_SHA256 from this card RUN00'}else{'pending first preflight'});
  DeviceMemorySize='0x1000';HardwareTested=$false;
  EfiSha256=(Get-FileHash -LiteralPath "$out/$name.EFI" -Algorithm SHA256).Hash;
  Headers=@(Get-ChildItem -LiteralPath . -Recurse -File -Filter '*.h' | Where-Object {$_.FullName -notlike '*\build\*'} | Sort-Object FullName | ForEach-Object {[ordered]@{Path=$_.FullName.Substring($PSScriptRoot.Length+1);Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}});
  Sources=@($sources | ForEach-Object {[ordered]@{Path="$_.c";Sha256=(Get-FileHash -LiteralPath "$_.c" -Algorithm SHA256).Hash}})
  } | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath "$out/build-manifest.json"
 "Built $name.EFI; ExperimentalUnlock=$([bool]$ExperimentalUnlock); hardware untested."
} finally {Pop-Location}
