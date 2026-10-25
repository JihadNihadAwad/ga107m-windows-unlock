# GA107M unlock persistence on Windows: findings and open questions

Status after RUN03: the UEFI transaction is fully working on hardware
(FWSEC -> V67 -> SS0=0x88888888 -> FLR -> clean handoff), but the FP16/BF16
issue rate reads locked (512-class) by the time CUDA runs in Windows.

## What the 610.43.03 open kernel-module source establishes

- The open driver never writes the selector registers. Greps for
  `0x82381c`/`0x823820`/`0x823804`, `FEAT_OVR`, and `REG_WR32.*8238`
  return nothing across the whole source tree.
- `NV2080_CTRL_CMD_GR_GET_SM_ISSUE_RATE_MODIFIER` (ctrl2080gr.h) is
  GET-only. The values come from `KGRAPHICS_STATIC_INFO`, fetched via an
  internal RPC (`NV2080_CTRL_CMD_INTERNAL_STATIC_KGR_GET_SM_ISSUE_RATE_MODIFIER`)
  from the GSP-RM at init. There is no SET variant anywhere in the SDK.
- Therefore: no patchable write in the open driver, no registry key, no
  user-facing API. Selector defaults are programmed by the card's own
  boot firmware path (GSP-ROM/PWRMGMT/fuse-init) and/or inside the closed
  GSP-RM binary.

## Evidence for the D3cold re-init theory (prime suspect)

- Cold-entry SS0 varies every boot on this card (0x53520035, 0x13720035,
  0x13520035): the card's own init programs the selectors from fuses at
  each power-on (with per-boot entropy), so any power removal re-writes
  them.
- This is a hybrid (Optimus) laptop: the iGPU drives the display, so the
  dGPU has zero clients at the Windows desktop. The driver parks it in
  runtime D3 (D3cold) within seconds; DEVPKEY_PciDevice_D3ColdSupport=1036
  confirms D3cold is active. First CUDA use = power-on = fuse re-init =
  SKU-default selectors.
- The LG Linux result (unlock survives driver bind and doubles throughput
  in the same boot) is consistent: on Linux, dynamic power management
  (RTD3) is opt-in via `NVreg_DynamicPowerManagement` and default-off on
  CachyOS. The driver-side GSP boot does NOT rewrite SS0; the Linux kmod
  also re-applies at every boot regardless.
- On Windows RTD3 is always-on for hybrid dGPUs and cannot be disabled
  through any supported knob (INF has no RID3-style registry key;
  "Prefer maximum performance" affects P-states, not D3).

## Windows driver firmware (617.14, DriverStore nvhmi.inf_amd64_8d155ac014c1a4ba)

- `gsp_ga10x.bin` 27,043,992 B sha256 CE9E36A7F078456768417B00979AF0BA5ADD568FF7A935749EBD6FA348BF9AA3
  (different container from the pinned Linux 84 MB image; serves the
  WSL/paravirt path)
- `nvcoproc.bin` 12,699,468 B and `ucodes_ga10x.bin` 55.5 MB: the native
  Windows RM/falcon containers
- All are inside NV_DISP.CAT-signed payloads; patching any of them breaks
  the production signature chain checked by the BootROM/Booter at GSP
  boot -> error 43. Not a viable route without a signature bug like V67
  in the boot path we control (which is exactly what the UEFI transaction
  already does pre-boot).

## Experiment matrix

1. HOLD test (cheap): unlock boot -> immediately start `tools/hold_gpu_windowed.py`
   at logon -> run `tools/bench_gemm.py` while held. If 1024-class: the unlock
   survives driver bind and only D3cold clears it -> persistence = keep
   the GPU awake (autostart hold). If 512-class: the card parked before
   the hold started (ambiguous) or the loss is elsewhere.

   **RESULT (2026-10-25, RUN04 + hold): CONFIRMED.**
   FP16 987 / BF16 962 ops/SM/clk (1024-class) vs 513/507 locked.
   TF32 unchanged at 250 (SS1 preserved by the SS0-only policy - the
   opt-in SS1 experiment would target 512). FP16 reached 25.77 TFLOPS at
   1305 MHz / ~48.5 W vs 16.67 TFLOPS at 1657 MHz / ~59 W locked: with
   double the issue rate the governor settles at a lower clock for the
   same work - ~55% more throughput at ~20% less power.
   RUN04 transaction itself: result=0, SS0=0x88888888 held through FLR,
   clean handoff (identical to RUN03).

   Operational conclusion: the unlock persists for exactly as long as the
   dGPU never enters D3cold. A live CUDA context (hold_gpu.py) is a
   sufficient client. Autostart it with install-hold-task.ps1 so it wins
   the race against the first park after logon. Battery cost is real
   (the GPU cannot power down), so remove the task when not wanted.
2. If hold test is negative, the remaining structural options are, in
   increasing cost:
   a. Device Manager power-tab checkbox (if exposed for the GPU) / BIOS
      discrete-only mode (changes EFI display policy; v1 refuses a target
      with an active GOP).
   b. Windows kernel driver that re-runs V67 + selector write after every
      D3cold exit - requires the pre-driver FWSEC/V67 dance on a live,
      bound card; upstream explicitly refuses this; high risk, hard.
   c. GSP container patching - signature-blocked as analyzed above.
