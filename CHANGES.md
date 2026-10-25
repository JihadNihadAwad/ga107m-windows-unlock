# Changes

## 2026-10-25 — RUN03/RUN04: successful hardware transactions; persistence solved

- RUN03/RUN04: the full transaction executed on live GA107M silicon —
  FWSEC halt clean (scratch=0, WPR2 established), V67 chain staged and run,
  SS0=0x88888888 written and verified pre- and post-FLR, GSP left halted,
  BME contained, clean Windows handoff (`transaction result=0 clean=1`).
  First documented Windows/UEFI-side GA107M laptop unlock.
- Benchmark after RUN04 + hold_gpu.py: FP16 987 / BF16 962 ops/SM/clk
  (1024-class; locked baseline 513/507). TF32 unchanged per the
  entry-latched SS1-preserve policy. Unlocked FP16 delivers ~55% more
  TFLOPS at ~20% lower power (issue-rate doubling lets the governor run
  the same GEMM at a lower clock).
- Persistence root cause proven: runtime D3cold. The card's fuse-init
  re-programs the selectors at every power-on (entry SS0 has per-boot
  entropy), Windows RTD3 power-gates the idle dGPU, first CUDA use
  re-inits to SKU defaults. A live CUDA context from before the first
  park preserves the unlock (hold_gpu.py; install-hold-task.ps1
  autostarts it at logon). See docs/GA107M-PERSISTENCE.en.md.
- Fixes accumulated across the boot-test ladder (RUN00-04): cold
  decode-off target snapshot, DEVSTS-vs-drift discrimination, GOP console
  mirror classification, probe/preflight/experimental build-mode gates,
  real-PROM transaction + sha fixtures in the cross-host suite.

## 2026-10-24 — GA107M port: RTX 3050 Ti Laptop (HP 10de:25a0/103c:88e5) preflight

Fork branch `ga107m-3050ti`. The upstream RTX 3090 FE transaction core is
retained; the exact-card identity, display policy and profile pins now target
an HP RTX 3050 Ti Laptop GPU (GA107M, 4 GiB, VBIOS 94.07.3a.00.6e, BDF
0000:01:00.0) with the Intel Iris Xe iGPU (8086:9a49/103c:88e5, 00:02.0)
providing the EFI console.

- Ported the GA107 0.1.1 laptop-FWSEC fix from IHaBiS02/ga107-unlock into the
  vendored core: `gpu_unlock_fwsec_dmem_transfer` (vbios_610.c/.h) plus the
  call-site change that validates and transfers the 256-byte-rounded DMEM
  length instead of the declared size (live LG geometry 0x980 -> 0xa00).
  Regression test `tests/test_vbios_dmem.c` passes.
- Device profile (port.h): `GU_BOARD_ID 0x25a010de`, `GU_BOARD_SUBSYSTEM
  0x88e5103c`, `GU_DEVICE_MEMORY_SIZE 0x1000`, Intel iGOP display guards.
  Exact-card cold-state pins (chip register, GFW values, FEAT PLM, PROM
  SHA-256, BAR0 base, config capability dword) are PENDING and fail closed:
  the experimental build raises `#error` until `GU_CARD_PROFILE_PINNED` is
  defined from this card's own preflight REPORT.
- Preflight builds are now a read-only inventory: config dump, BAR0
  descriptor, live PROM SHA-256 + `PROM.BIN` capture, FWSEC descriptor parse
  and cold-state registers are logged even when profile pins do not match,
  so one boot produces every value needed to pin the profile. No GPU writes,
  DMA, reset, ownership or driver release occur in these builds.
- DisplayCheck rewritten for hybrid-laptop topology: GOP handles are resolved
  to PCI ancestors; the console must come from the Intel iGOP (00:02.0) and
  the target must expose no GOP (enforced in experimental builds, logged in
  preflight). The ASUS AMD-GOP path checks were removed with this port.
- ColdCheck experimental pins parameterized (`GU_EXPECTED_*`); the
  3090-only fixed-SS0 stock profiles (0x03100062/0x03000062) are no longer
  offered by build scripts; entry-latched SS1-preserve is the default
  profile (matches the ga107-unlock default policy), entry-latched SS1-full
  remains an explicit opt-in.
- `build-wsl.sh` added: cross-builds the EFI app with clang/lld-link from
  WSL when a Windows LLVM install is not available. PE verification passes
  (AMD64, subsystem 10, zero imports, NX).
- `GU_EXPERIMENTAL_3090` remains the upstream build-gate macro name in
  shared sources and tests; in this fork it gates the GA107M experimental
  build. The 3090-specific host test fixtures (test_3090_preflight,
  test_3090_experimental) no longer match this profile and are excluded
  until this card's own RUN00 fixtures exist.

## 2026-10-24 — v0.3.1 variable headless display-child discovery

- Fixed preflight rejection when ASUS firmware exposes three RTX display
  children instead of exactly two. The wrapper now discovers and validates up
  to four unique RTX display children, then releases each guarded EDID
  connection before the NVIDIA GOP.
- The rebuilt wrapper completed automatic unlock and Windows handoff on the
  same system. The user observed the RTX display go dark during release and
  return after Windows started. No new performance benchmark was run for this
  build.
- Release remains source-only. Do not publish local EFI binaries, firmware,
  PROM captures, raw boot logs, partition identifiers or machine-local config.

## 2026-10-24 — v0.3.0 headless internal handoff, public setup and recovery

- Verified one internal headless boot on the exact ASUS BIOS 3057 / RTX 3090 FE
  system: RTX GOP 1 → 0, one successful unlock transaction, direct Windows
  return and three Tensor benchmark trials. FP16/BF16 medians were
  124.969948/128.931534 TFLOPS. No active AMD display was required; the CPU
  still has an iGPU, and other boards/CPUs/headless USB are not validated.
- Added a separate headless compile profile: two reviewed EDID children, exact
  NVIDIA ownership release, zero-physical-GOP policy, file-only child logging,
  TPL-protected observations and original PROM/cold/containment/cleanup gates.
- Added public configuration/build provenance, stage/register/confirmed automatic
  promotion and Windows-first recovery. Registration preserves default priority;
  promotion explicitly orders Headless then Windows and saves Fast Startup
  recovery data before mutation. Source/include/config/asset contracts are rechecked.
- Rewrote English/Korean README and setup guides, retained distinct legacy USB
  and AMD-display paths, and published sanitized headless evidence. Firmware,
  PROM/BIOS, private partition configuration and generated EFI images stay local.
- Original manual child, automatic child and automatic wrapper EFI hashes remain
  identical. Public installer mutation paths are mock-tested; cross-machine
  registration, repeated cold-boot reliability and sleep/resume remain unverified.

## 2026-10-23 — v0.2.0 internal handoff and experimental automatic boot

- Added a separate `internal-boot/` source tree; original USB release source and EFI remain unchanged.
- Reviewed RTX console/EDID/GOP release, original IO/MEM restoration without BME, and direct Windows handoff. Original unlock transaction and card guards retained.
- Discovered display ADR per boot while pinning PCI ancestry, GPU identity and subsequent whole-path checks. Allowed only old CED/URD status clearing after successful unlock; other config changes still fail.
- Added precise handoff failure stages and config differences. Internal manual handoff and Tensor performance were verified on one BIOS 3057 system.
- Added a separate compile-time automatic child with two Esc windows, strict input failures, fresh hardware gates and rejection of unexpected owners. Automatic firmware boot remains unverified.
- Added local partition configuration, builds, stage/manual/automatic/recovery scripts and bilingual walkthroughs. Private partition GUIDs, firmware/PROM and raw boot graphs are excluded.
- Public-source builds reproduced all four locally installed EFI hashes. Public installer registration/promotion remains experimental; host and policy tests are not hardware evidence.

## 2026-10-23 — International documentation and compatibility inventory

- English default README and complete English USB, assets, build, compatibility, benchmark and results guides; Korean README and guides remain available through language links.
- Added a local read-only Windows model/version inventory and manual compatibility issue form. Private identifier fields and PnP instance suffixes are excluded; reports never authorize unlock.
- Added a staged portability design distinguishing host discovery, GPU policy, software tests and hardware evidence. This update adds no new write-enabled EFI profile or hardware support.
- USB package readme links both languages. Refreshed the public source inventory for these documentation/tool additions; successful EFI and its corresponding C/header hashes are unchanged.

## 2026-10-20 — Windows GPU UNLOCK public case study

- Base: rough2515-hub/rtx3080-unlock-public at a08254d269af30ace9e57237abd3d8e6c261e8f5; original core and licenses retained.
- Local changes made 2026-10-19/20: exact RTX3090 24GiB and AMD iGPU identity checks, memory layout, firmware ownership/PCI access checks, default read-only mode, explicit experimental profiles, immutable entry-selector latch and one SS1 write for entryLatchedSs1Full, phase guards, corresponding host mock tests.
- Modified upstream files: unlock/app.c, app_io.c, build.ps1, checks.c, checks.h, port.c, port.h, test-all.ps1, tests/test_app.c, test_checks.c, test_efi_transaction.c, test_master_attributes.c, test_pe.py, test_port.c, test_sha256.c, vendor/unlock_610.c. Additional RTX3090 tests are under unlock/tests/.
- Public packaging only: portable LLVM defaults, configurable MSVC environment and PE negative tests that inspect the selected profile rather than requiring a separate preflight build; no further GPU transaction changes. Success EFI is tied to hashes of the corresponding C sources and headers. Changed source files carry dated notices; C notices share an existing line to preserve diagnostic __LINE__ values and reproduce the successful EFI byte for byte.
- Added Korean USB/ASUS boot handoff guide, measured results and benchmark sources. Changed benchmark device selection to RTX3090, added inspect-only mode, and kept the original GEMM helper/measure path.
- The release deliberately excludes NVIDIA firmware, drivers, CUDA DLLs, original PROM dumps, private paths, credentials and unrelated repositories.
- Direct Windows chainload and internal disk automatic boot are not implemented.
