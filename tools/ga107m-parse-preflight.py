#!/usr/bin/env python3
"""Parse a GA107M preflight REPORT.TXT and emit the exact-card profile pins.

Usage: python ga107m-parse-preflight.py <path-to-RUN00\\REPORT.TXT>

Reads the read-only inventory lines written by GA107M-PREFLIGHT.EFI and
prints a ready-to-transcribe port.h block plus sanity findings. Nothing is
written automatically; transcribe into unlock/port.h by hand and define
GU_CARD_PROFILE_PINNED when every pin matches this card's own report.
"""
import re
import sys

REQUIRED = [
    ("preflight pins", r"preflight pins for profile transcription: chip=((?:0x)?[0-9a-f]+) gfw_plm=((?:0x)?[0-9a-f]+) gfw_boot=((?:0x)?[0-9a-f]+) gfw_cpuctl=((?:0x)?[0-9a-f]+) wpr2_hi=((?:0x)?[0-9a-f]+) feat_plm=((?:0x)?[0-9a-f]+) memory=((?:0x)?[0-9a-f]+) ss0=((?:0x)?[0-9a-f]+) ss1=((?:0x)?[0-9a-f]+)"),
    ("prom hash", r"PROM sha256=([0-9a-f]{64})"),
    ("bar0 observed", r"bar0 observed: base=((?:0x)?[0-9a-f]+) size=((?:0x)?[0-9a-f]+)"),
    ("fwsec", r"FWSEC descriptor: image_off=((?:0x)?[0-9a-f]+) image_size=((?:0x)?[0-9a-f]+) imem_pa=((?:0x)?[0-9a-f]+) imem_size=((?:0x)?[0-9a-f]+) imem_va=((?:0x)?[0-9a-f]+) dmem_pa=((?:0x)?[0-9a-f]+) data_off=((?:0x)?[0-9a-f]+) dmem_size=((?:0x)?[0-9a-f]+) pkc_off=((?:0x)?[0-9a-f]+) interface_off=((?:0x)?[0-9a-f]+) ucode_id=((?:0x)?[0-9a-f]+) engine_mask=((?:0x)?[0-9a-f]+) sig_count=((?:0x)?[0-9a-f]+) sig_versions=((?:0x)?[0-9a-f]+) sig_off=((?:0x)?[0-9a-f]+) sig_bytes=((?:0x)?[0-9a-f]+) dmem_transfer=((?:0x)?[0-9a-f]+) ret=(-?\d+)"),
]

def hx(v):
    """Normalize a matched hex token to 0x-prefixed lowercase."""
    return f"0x{int(v, 16):x}"

def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    text = open(sys.argv[1], "r", errors="replace").read()
    # Probe reports contain two cold-state blocks: pre-decode (erased-flash)
    # and post-decode (real values). The pins must come from the LAST block.
    blocks = re.findall(
        r"preflight pins for profile transcription: (.*?)\n", text)
    if not blocks:
        print("MISSING: no 'preflight pins' line found in report")
        return 1
    pins_line = blocks[-1]
    prom_blocks = re.findall(r"PROM sha256=([0-9a-f]{64})", text)
    if not prom_blocks:
        print("MISSING: no 'PROM sha256' line found in report")
        return 1
    prom_hash = prom_blocks[-1]
    bar_blocks = re.findall(
        r"bar0 latched: base=((?:0x)?[0-9a-f]+) size=((?:0x)?[0-9a-f]+)",
        text) or re.findall(
        r"bar0 observed: base=((?:0x)?[0-9a-f]+) size=((?:0x)?[0-9a-f]+)",
        text)
    if not bar_blocks:
        print("MISSING: no 'bar0 observed' line found in report")
        return 1
    bar_base, bar_size = [hx(v) for v in bar_blocks[-1]]
    fwsec_m = None
    if "FWSEC parse: not found in PROM dump" in text:
        print("NOTE: PROM parse found no FWSEC (decode was off; expected in the "
              "plain preflight). A PROBE report is needed for real values.")
    for m in re.finditer(
        r"FWSEC descriptor: image_off=((?:0x)?[0-9a-f]+) image_size=((?:0x)?[0-9a-f]+) imem_pa=((?:0x)?[0-9a-f]+) imem_size=((?:0x)?[0-9a-f]+) imem_va=((?:0x)?[0-9a-f]+) dmem_pa=((?:0x)?[0-9a-f]+) data_off=((?:0x)?[0-9a-f]+) dmem_size=((?:0x)?[0-9a-f]+) pkc_off=((?:0x)?[0-9a-f]+) interface_off=((?:0x)?[0-9a-f]+) ucode_id=((?:0x)?[0-9a-f]+) engine_mask=((?:0x)?[0-9a-f]+) sig_count=((?:0x)?[0-9a-f]+) sig_versions=((?:0x)?[0-9a-f]+) sig_off=((?:0x)?[0-9a-f]+) sig_bytes=((?:0x)?[0-9a-f]+) dmem_transfer=((?:0x)?[0-9a-f]+) ret=(-?\d+)",
        text):
        fwsec_m = m
    if fwsec_m is None:
        print("MISSING: no 'FWSEC descriptor' line found in report "
              "(decode-off preflight reports cannot parse the PROM)")
        return 1
    pin_re = re.compile(
        r"chip=((?:0x)?[0-9a-f]+) gfw_plm=((?:0x)?[0-9a-f]+) gfw_boot=((?:0x)?[0-9a-f]+) "
        r"gfw_cpuctl=((?:0x)?[0-9a-f]+) wpr2_hi=((?:0x)?[0-9a-f]+) feat_plm=((?:0x)?[0-9a-f]+) "
        r"memory=((?:0x)?[0-9a-f]+) ss0=((?:0x)?[0-9a-f]+) ss1=((?:0x)?[0-9a-f]+)")
    pm = pin_re.search(pins_line)
    if not pm:
        print("MISSING: last cold-state block is malformed")
        return 1
    found = {
        "preflight pins": pm.groups(),
        "prom hash": (prom_hash,),
        "bar0 observed": (bar_base, bar_size),
        "fwsec": fwsec_m.groups(),
    }
    (chip, gfw_plm, gfw_boot, gfw_cpuctl, wpr2_hi, feat_plm,
     memory, ss0, ss1) = [hx(v) for v in found["preflight pins"]]
    fw = found["fwsec"]
    (fw_image_off, fw_image_size, fw_imem_pa, fw_imem_size, fw_imem_va,
     fw_dmem_pa, fw_data_off, fw_dmem_size, fw_pkc_off, fw_interface_off,
     fw_ucode_id, fw_engine_mask, fw_sig_count, fw_sig_versions,
     fw_sig_off, fw_sig_bytes, fw_dmem_transfer, fw_dmem_ret) = \
        [hx(v) for v in fw[:17]] + [fw[17]]
    decode_seen = "decode enable result: command=0x" in text
    if len(blocks) > 1 and not decode_seen:
        issues = ["report has multiple cold blocks but no decode-enable result"]
    if int(chip, 16) == 0xffffffff:
        print("STALE: last cold block still reads erased-flash values (0xffffffff);")
        print("       a PROBE report with a successful decode-enable is required.")
        return 1

    print("=== GA107M exact-card profile pins (transcribe into unlock/port.h) ===\n")
    print(f"#define GU_CARD_PROFILE_PINNED 1")
    print(f"#define GU_BAR0_BASE {bar_base}ULL")
    print(f'#define GU_CARD_PROFILE_PROM_SHA256 "{prom_hash}"')
    print(f"#define GU_EXPECTED_CHIP {chip}u")
    print(f"#define GU_EXPECTED_GFW_PLM {gfw_plm}u")
    print(f"#define GU_EXPECTED_GFW_BOOT {gfw_boot}u")
    print(f"#define GU_EXPECTED_GFW_CPUCTL {gfw_cpuctl}u")
    print(f"#define GU_EXPECTED_WPR2_HI {wpr2_hi}u")
    print(f"#define GU_EXPECTED_FEAT_PLM {feat_plm}u")
    print(f"#define GU_DEVICE_MEMORY_SIZE {memory}u" if memory != "0x1000"
          else "/* GU_DEVICE_MEMORY_SIZE 0x1000U already matches */")
    print(f"/* entry selectors observed (entry-latched, not pinned): SS0={ss0} SS1={ss1} */")

    print("\n=== Sanity findings ===")
    issues = []
    if memory != "0x1000":
        issues.append(f"memory register reads {memory}, expected 0x1000 (4 GiB)")
    if int(fw_engine_mask, 16) != 0x400:
        issues.append(f"FWSEC engine_mask={fw_engine_mask}, transaction expects 0x400")
    if fw_dmem_ret != "0":
        issues.append(f"FWSEC dmem_transfer failed in report (ret={fw_dmem_ret})")
    else:
        print(f"FWSEC dmem round-up OK: {fw_dmem_size} -> {fw_dmem_transfer} "
              f"(image {fw_image_size}, data_off {fw_data_off})")
    if bar_size != "0x1000000":
        issues.append(f"BAR0 size {bar_size}, expected 0x1000000")
    nvidia = re.findall(r"GOP\[\d+\] belongs to the NVIDIA unlock target", text)
    print(f"NVIDIA GOPs active in EFI: {len(nvidia)} (must be 0 for the v1 experimental build)")
    if nvidia:
        issues.append(f"{len(nvidia)} GOP handle(s) belong to the unlock target; "
                      "v1 policy refuses a card with its own active GOP")
    gop_result = re.search(r"GOP result: intel=(\d+) nvidia=(\d+) other=(\d+)", text)
    if gop_result:
        print(f"GOP classification: intel={gop_result.group(1)} "
              f"nvidia={gop_result.group(2)} other={gop_result.group(3)}")
    cold = re.findall(r"cold state: command=0x[0-9a-f]+ chip=(0x[0-9a-f]+)", text)
    if cold:
        print(f"cold chip register (last block): {cold[-1]}")
    elif re.findall(r"cold state: chip=(0x[0-9a-f]+)", text):
        print(f"cold chip register (legacy format): "
              f"{re.findall(r'cold state: chip=(0x[0-9a-f]+)', text)[-1]}")

    # Decode the LAST config dump (post-decode in probe reports).
    config_dumps = list(re.finditer(
        r"config\[([0-9a-f]{2})\]=([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})", text))
    dwords = [None] * 64
    for m in config_dumps:
        base = int(m.group(1), 16)
        for k in range(4):
            dwords[(base // 4) + k] = int(m.group(2 + k), 16)
    if all(v is not None for v in dwords):
        def byte(off):
            return (dwords[off // 4] >> ((off % 4) * 8)) & 0xFF
        def word(off):
            return byte(off) | (byte(off + 1) << 8)
        print(f"\nPCI config: id={dwords[0]:08x} subsystem={dwords[0x2c // 4]:08x} "
              f"command={word(4):04x} status={word(6):04x} "
              f"class_rev={dwords[2]:08x} BAR0_low={dwords[4]:08x}")
        ptr = byte(0x34)
        print("Capability chain:")
        seen = set()
        names = {0x01: "PCIe", 0x05: "MSI", 0x11: "MSI-X", 0x09: "Vendor",
                 0x42: "PCIe Sec"}
        while ptr and ptr not in seen and len(seen) < 16:
            seen.add(ptr)
            cid, nxt = byte(ptr), byte(ptr + 1)
            print(f"  offset=0x{ptr:02x} id=0x{cid:02x}"
                  f" ({names.get(cid, '?')}) next=0x{nxt:02x} "
                  f"dword@off=0x{dwords[ptr // 4]:08x}")
            ptr = nxt
        bdf = re.search(r"target BDF=(\d+):(\d+):(\d+)\.(\d+) checks=(\d+)", text)
        if bdf:
            print(f"target BDF={bdf.group(1)}:{bdf.group(2)}:{bdf.group(3)}."
                  f"{bdf.group(4)} structural_checks={bdf.group(5)}")
    else:
        issues.append("config dump incomplete in report (expected 16 config[xx] lines)")
    if issues:
        print("\nISSUES (resolve before building the experimental profile):")
        for i in issues:
            print(f"  - {i}")
        return 1
    print("\nAll sanity checks passed. Safe to build the experimental profile "
          "(after manual review) with -ExperimentalUnlock -StockProfile entryLatchedSs1Preserve.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
