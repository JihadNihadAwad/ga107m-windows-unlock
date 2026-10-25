#!/bin/bash
# Cross-host the Windows test suite from WSL using clang -target x86_64-pc-windows-msvc
# against the installed VS2022 MSVC/SDK headers and libs. No Windows LLVM needed.
# Mirrors the no-fixture jobs of unlock/test-all.ps1.
# MSVC/SDK paths can be overridden with MSVC_DIR / SDK_INC / SDK_LIB env vars.
set -e
cd "$(dirname "$0")"
OUT=build/tests-cross
mkdir -p "$OUT"
CLANG=clang
MSVC="${MSVC_DIR:-/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207}"
SDKI="${SDK_INC:-/mnt/c/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0}"
SDKL="${SDK_LIB:-/mnt/c/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0}"
T=(-target x86_64-pc-windows-msvc -fuse-ld=lld-link)
INC=(-isystem "$MSVC/include" -isystem "$SDKI/ucrt" -isystem "$SDKI/um" -isystem "$SDKI/shared")
CRT=(-llibcmt -llibvcruntime -llibucrt)
LDFLAGS=(-L"$MSVC/lib/x64" -L"$SDKL/ucrt/x64" -L"$SDKL/um/x64")
FLAGS=(-std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function
       -D_CRT_SECURE_NO_WARNINGS -Icompat -I../deps/edk2/MdePkg/Include -I../deps/edk2/MdePkg/Include/X64)
DEFS=(-D__KERNEL__ -DGU_UEFI_PORT -DGU_TARGET_PCI_ID=0x25A010deU)
CORE=(port.c compat.c vendor/unlock_610.c vendor/prep_610.c vendor/layout_610.c vendor/vbios_610.c vendor/gfw_eval.c)

echo "== sanity: trivial CRT program =="
printf '#include <stdio.h>\n#include <malloc.h>\nint main(void){void*p=_aligned_malloc(4096,4096);printf("aligned=%%p\\n",p);_aligned_free(p);return 0;}\n' > /tmp/sanity.c
$CLANG "${T[@]}" "${INC[@]}" /tmp/sanity.c "${LDFLAGS[@]}" -o "$OUT/sanity.exe" "${CRT[@]}" && echo "sanity link OK (run from Windows)"

build_run() {
  NAME="$1"; shift
  SOURCES="$1"; shift
  EXTRA="$1"; shift
  echo "== build $NAME =="
  # shellcheck disable=SC2086
  $CLANG "${T[@]}" "${INC[@]}" "${FLAGS[@]}" ${EXTRA:-} ${SOURCES} "${LDFLAGS[@]}" -o "$OUT/$NAME.exe" "${CRT[@]}"
  echo "built $OUT/$NAME.exe (run from Windows)"
}

# DMA / MMIO / transaction lock tests exercise the experimental write path:
# they compile with GU_EXPERIMENTAL_3090 + the preserve profile so the gates
# match the production experimental build (test_port uses GU_HOST_WRITE_TEST
# for the raw-adapter cases, mirroring upstream test-all.ps1).

build_run port "tests/test_port.c port.c" "-DGU_HOST_WRITE_TEST -DGU_EXPERIMENTAL_3090 -DGU_ENTRY_LATCHED_SS1_PRESERVE"
build_run write-lock "tests/test_preflight_lock.c port.c" ""
build_run master-attributes "tests/test_master_attributes.c ${CORE[*]}" "${DEFS[*]} -DGU_HOST_WRITE_TEST -DGU_EXPERIMENTAL_3090 -DGU_ENTRY_LATCHED_SS1_PRESERVE"
$CLANG "${T[@]}" "${INC[@]}" "${FLAGS[@]}" -fno-builtin -Dmemcpy=GuTestMemcpy -Dmemset=GuTestMemset -Dmemchr=GuTestMemchr -Dmemcmp=GuTestMemcmp -Dstrcmp=GuTestStrcmp -Dstrlen=GuTestStrlen -c runtime.c -o "$OUT/runtime-host.obj"
build_run runtime "tests/test_runtime.c format.c $OUT/runtime-host.obj" "-fno-builtin"
build_run profile-lock-preserve "tests/test_profile_lock.c port.c" "-DGU_EXPERIMENTAL_3090 -DGU_ENTRY_LATCHED_SS1_PRESERVE"
build_run profile-lock-full "tests/test_profile_lock.c port.c" "-DGU_EXPERIMENTAL_3090 -DGU_ENTRY_LATCHED_SS1_FULL"
build_run vbios-dmem "tests/test_vbios_dmem.c vendor/vbios_610.c" ""

# Full-transaction mock against the real captured PROM and the pinned assets.
# Built here (path-independent); run by run-cross-tests.ps1 with Windows paths.
build_run core-transaction "tests/test_efi_transaction.c ${CORE[*]}" "${DEFS[*]} -DGU_EXPERIMENTAL_3090 -DGU_ENTRY_LATCHED_SS1_PRESERVE"
build_run sha-realprom "tests/test_sha256.c sha256.c" ""

echo "== stock-selection (compile-time cases; parameter checks run on Windows) =="
# clang-msvc wrapper so the python harness compiles with the cross target
cat > /tmp/clang-msvc <<'EOF'
#!/bin/sh
exec clang -target x86_64-pc-windows-msvc "$@"
EOF
chmod +x /tmp/clang-msvc
GU_SKIP_PS_CHECKS=1 python3 tests/test_stock_selection.py /tmp/clang-msvc

echo "ALL CROSS-HOST TEST BINARIES BUILT; run them from Windows (run-cross-tests.ps1)"
