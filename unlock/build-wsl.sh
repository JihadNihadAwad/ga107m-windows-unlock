#!/bin/bash
# Cross-build the GA107M EFI app from WSL when a Windows LLVM install is unavailable.
# Mirrors unlock/build.ps1 exactly (same sources, flags, link and output names).
# Usage:  bash build-wsl.sh [--experimental <profile>]   profile: entryLatchedSs1Preserve | entryLatchedSs1Full
set -e
cd "$(dirname "$0")"
CLANG=${CLANG:-clang}
LLD_LINK=${LLD_LINK:-lld-link}
EXPERIMENTAL=0
PROFILE=entryLatchedSs1Preserve
if [ "$1" = "--experimental" ]; then
  EXPERIMENTAL=1
  PROFILE=${2:-entryLatchedSs1Preserve}
  case "$PROFILE" in
    entryLatchedSs1Preserve|entryLatchedSs1Full) ;;
    *) echo "profile must be entryLatchedSs1Preserve or entryLatchedSs1Full" >&2; exit 2 ;;
  esac
fi

DEFINES=""
NAME=GA107M-PREFLIGHT
OUT=build
if [ "$EXPERIMENTAL" = 1 ]; then
  case "$PROFILE" in
    entryLatchedSs1Preserve) DEFINES="-DGU_ENTRY_LATCHED_SS1_PRESERVE" ;;
    entryLatchedSs1Full) DEFINES="-DGU_ENTRY_LATCHED_SS1_FULL" ;;
  esac
  DEFINES="-DGU_EXPERIMENTAL_3090 $DEFINES"
  NAME="GA107M-EXPERIMENTAL-$PROFILE"
  OUT="build/experimental/$PROFILE"
elif [ "$1" = "--probe" ]; then
  # Read-only probe: enables only the PCI MEM decode bit (the same config
  # write the Windows driver performs on every boot), reads the cold
  # registers and real PROM for profile transcription, then returns.
  # No MMIO writes, no DMA, no firmware execution, no selector writes.
  DEFINES="-DGU_PROBE_DECODE"
  NAME="GA107M-PROBE"
  OUT="build/probe"
fi
mkdir -p "$OUT"
SOURCES="app app_io ownership format runtime sha256 checks port compat vendor/unlock_610 vendor/prep_610 vendor/layout_610 vendor/vbios_610 vendor/gfw_eval"
OBJECTS=""
for src in $SOURCES; do
  obj="$OUT/$(echo "$src" | tr '/' '_').obj"
  echo "CC $src.c"
  $CLANG -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -Wno-unused-parameter -D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x25A010deU $DEFINES -I compat -I ../deps/edk2/MdePkg/Include -I ../deps/edk2/MdePkg/Include/X64 -c "$src.c" -o "$obj"
  OBJECTS="$OBJECTS $obj"
done
echo "LINK $NAME.EFI"
$LLD_LINK /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf "/map:$OUT/$NAME.map" "/out:$OUT/$NAME.EFI" $OBJECTS
echo "Built $OUT/$NAME.EFI (ExperimentalUnlock=$EXPERIMENTAL)"
