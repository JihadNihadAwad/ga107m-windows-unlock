# Verified hardware and route-specific requirements

**English** | [한국어](COMPATIBILITY.ko.md)

> **This fork (GA107M laptop port):** the hardware-verified case is an
> **HP RTX 3050 Ti Laptop GPU (GA107M)**, `10de:25a0` / subsystem `103c:88e5`,
> 4 GiB, VBIOS `94.07.3a.00.6e`, Intel Iris Xe display, driver 617.14, with
> two clean UEFI transactions and measured results in
> [GA107M.en.md](GA107M.en.md). The RTX 3090 tables below are the upstream
> project's separate verified case, preserved unchanged. Everything the
> checks enforce for this fork's card is transcribed in `unlock/port.h`
> (fails closed with `#error` until a card's own probe report provides the
> pins); see the [GA107M workflow](GA107M.en.md) for what was actually
> validated and what was not.

The current implementation checks more than a model name. v0.3.0 records one
successful internal headless boot on the same machine used for the earlier
AMD-display trials. It does not make other RTX 3090s or platforms supported.

| Common GPU/platform item | Validated value |
|---|---|
| Card | NVIDIA RTX 3090 Founders Edition, 24 GiB |
| PCI identity | `10de:2204`, subsystem `10de:1454` |
| GPU BDF | `0000:01:00.0` |
| Chip / VRAM register | `0xb72000a1` / `0x6000` MiB |
| BAR0 | `0xf5000000`, 16 MiB |
| PROM SHA256 | `8ea297373e9e5d3600ff41ad9fae88f2d14c98c017d100669f7333981da673b4` |
| Motherboard / BIOS | ASUS TUF GAMING B650M-PLUS WIFI / 3057 |
| CPU | AMD Ryzen 7 7800X3D |
| Recorded Windows / driver | Windows 11 Home 26200 / NVIDIA 617.14 |
| UEFI / Secure Boot | x64 / disabled |
| Selector profile | `entryLatchedSs1Full`; targets SS0 `0x88888888`, SS1 `0x8` |

These are documented successful values, not an authorization list. Code also
checks configuration/capabilities, PROM, cold state, driver code, ownership,
DMA cleanup and failure containment. Internal EFI builds additionally contain
the user's own Windows/dedicated ESP GUID configuration.

| Route | Display policy and validation |
|---|---|
| Legacy USB / ordinary internal builds | Preserve AMD physical GOP, PCI `1002:164e`, subsystem `1043:8877`, BDF `0000:0c:00.0` and validated display ancestry |
| Internal headless profile | One RTX physical GOP before release, zero physical GOP afterward; stable console/input interfaces, protected ownership graph, file-only child logging |
| Headless USB | Not tested; the ordinary USB EFI retains AMD requirements |

The headless test had all monitors on RTX and no active AMD GOP. The 7800X3D
still contains an iGPU; BIOS disablement is not established by a missing GOP.
**A CPU without an iGPU was not tested.** Neither Intel-host support nor
independence from every firmware's iGPU implementation is established.

| Other configuration | Status |
|---|---|
| Same exact machine, F8-selected headless automatic child | One unlock + Windows display recovery + Tensor measurement verified on 2026-10-24 |
| Default automatic startup across repeated cold boots | Not established by that one F8 trial |
| Another RTX 3090, even a Founders Edition in another PC | Unverified; exact checks may reject |
| Another ASUS BIOS, AMD board/CPU or Intel host | Unverified; separate profile and validation required |
| CPU without iGPU, multiple discrete GPUs, laptop/hybrid setup | Unverified |
| Other NVIDIA models/architectures | Separate GPU port and validation required |
| AMD Radeon / Intel discrete unlock target | No unlock implementation here |
| Games, ComfyUI, sleep/hibernate resume, long-term stability | Not established by GEMM/boot evidence |

See [headless instructions](HEADLESS.en.md), [legacy USB instructions](GUIDE.en.md)
and [sanitized evidence](../evidence/headless-handoff/README.md).
The ordinary source guards remain in [root unlock](../unlock/app.c); the separate
headless implementation is in [internal unlock](../internal-boot/unlock/app.c),
[headless policy](../internal-boot/unlock/headless-policy.h) and
[wrapper release policy](../internal-boot/boot/full-release.c).

Changing constants, replacing a PROM hash or deleting a display check is not a
validated port. New support requires memory/WPR layout, firmware compatibility,
ownership, DMA cleanup and failure-path evidence.
[Portability criteria](PORTABILITY.en.md) / [compatibility reporting](CONTRIBUTING.en.md).
