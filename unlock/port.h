/* Modified 2026-10-19/20 for Windows GPU UNLOCK; changes documented in CHANGES.md. */ #pragma once
#include <Uefi.h>
#include <Protocol/PciIo.h>
#include <Protocol/DevicePath.h>
#include <stddef.h>

/* GA107M port: exact card is an HP RTX 3050 Ti Laptop GPU.
 * PCI 10de:25a0, subsystem 103c:88e5, BDF 0000:01:00.0, 4 GiB, VBIOS 94.07.3a.00.6e.
 * The EFI console during the transaction is provided by the Intel Iris Xe iGPU:
 * PCI 8086:9a49, subsystem 103c:88e5, BDF 0000:00:02.0.
 * Cold-start note: this HP firmware leaves the dGPU with PCI decode disabled
 * (command=0x0000) until the driver binds, so RUN00's register/PROM reads were
 * erased-flash values; the experimental build enables decode itself first.
 */
#define GU_BOARD_ID 0x25a010deU
#define GU_BOARD_SUBSYSTEM 0x88e5103cU
#define GU_IGPU_ID 0x9a498086U
#define GU_IGPU_SUBSYSTEM 0x88e5103cU
#define GU_BAR0_BASE 0x52000000ULL /* observed RUN00 base; length-pinned, base latched per boot */
#define GU_DEVICE_MEMORY_SIZE 0x1000U
/* Exact-card cold-state profile. Values marked PENDING need one
 * decode-enabled cold read; the experimental build fails closed on them. */
#define GU_CARD_PROFILE_PROM_SHA256 "84e2444773201c9fd74d6e8984bf01d771433879f86e1c94dc91b2702f7949d7"
/* Cold pins transcribed from GA107M-PROBE RUN03 (decode-enabled read):
 *   cold state: command=0x100002 chip=0xb77000a1 gfw=0x8b8f/0x3ff/0x10
 *   WPR2_HI=0 SS0=0x53520035 SS1=0x5 memory=0x1000
 * GFW/FEAT-PLM values match the verified RTX 3090 cold profile exactly. */
#define GU_EXPECTED_CHIP 0xb77000a1u
#define GU_EXPECTED_GFW_PLM 0x8b8fu
#define GU_EXPECTED_GFW_BOOT 0x3ffu
#define GU_EXPECTED_GFW_CPUCTL 0x10u
#define GU_EXPECTED_WPR2_HI 0x0u
#define GU_EXPECTED_FEAT_PLM 0xffffff8fu
#define GU_EXPECTED_CONFIG_CAP_OFFSET 0xc8
#define GU_EXPECTED_CONFIG_CAP_VALUE 0x00050011u
#define GU_PROFILE_READY 1
#if defined(GU_EXPERIMENTAL_3090) && !GU_PROFILE_READY && __STDC_HOSTED__ != 1
#error "GA107M exact-card cold pins are not transcribed from a decode-enabled preflight yet"
#endif
/* The default artifact is read-only. Legacy host containment tests are never
   a production enable switch; experimental firmware requires its own define. */
#if defined(GU_HOST_WRITE_TEST) && __STDC_HOSTED__ != 1
#error GU_HOST_WRITE_TEST is forbidden in firmware builds
#endif
#if defined(GU_PROBE_DECODE) && (defined(GU_EXPERIMENTAL_3090) || defined(GU_HOST_WRITE_TEST))
#error "GU_PROBE_DECODE cannot be combined with experimental or write-test builds"
#endif
#if defined(GU_EXPERIMENTAL_3090) || defined(GU_PROBE_DECODE) || (defined(GU_HOST_WRITE_TEST) && __STDC_HOSTED__ == 1)
#define GU_PREFLIGHT_ONLY 0
#else
#define GU_PREFLIGHT_ONLY 1
#endif
/* Probe builds: exactly one decode-enable step plus reads; every other
   write path must stay as locked as the preflight. */
#ifdef GU_PROBE_DECODE
#define GU_PROBE_MODE 1
#else
#define GU_PROBE_MODE 0
#endif
/* GuWrite32 gates all MMIO writes on the experimental profile; probe and
   preflight builds must never pass an MMIO write. */
#if GU_PROBE_MODE
#define GU_MMIO_WRITE_ALLOWED 0
#elif defined(GU_EXPERIMENTAL_3090)
#define GU_MMIO_WRITE_ALLOWED 1
#else
#define GU_MMIO_WRITE_ALLOWED 0
#endif
#if defined(GU_STOCK_PROFILE_SS0) || defined(GU_STOCK_PROFILE_NAME)
#error Stock profile value and name must be derived from the explicit selector
#endif
#if (defined(GU_STOCK_PROFILE_ORIGINAL_03100062) + defined(GU_STOCK_PROFILE_OBSERVED_03000062) + defined(GU_ENTRY_LATCHED_SS1_PRESERVE) + defined(GU_ENTRY_LATCHED_SS1_FULL)) > 1
#error Select exactly one stock profile
#endif
#if defined(GU_EXPERIMENTAL_3090) && defined(GU_STOCK_PROFILE_ORIGINAL_03100062)
#define GU_STOCK_PROFILE_SS0 0x03100062U
#define GU_STOCK_PROFILE_NAME "original03100062"
#elif defined(GU_EXPERIMENTAL_3090) && defined(GU_STOCK_PROFILE_OBSERVED_03000062)
#define GU_STOCK_PROFILE_SS0 0x03000062U
#define GU_STOCK_PROFILE_NAME "observed03000062"
#elif defined(GU_EXPERIMENTAL_3090) && defined(GU_ENTRY_LATCHED_SS1_PRESERVE)
#define GU_STOCK_PROFILE_NAME "entryLatchedSs1Preserve"
#elif defined(GU_EXPERIMENTAL_3090) && defined(GU_ENTRY_LATCHED_SS1_FULL)
#define GU_STOCK_PROFILE_NAME "entryLatchedSs1Full"
#elif defined(GU_EXPERIMENTAL_3090)
#error Experimental firmware requires exactly one explicit stock profile
#elif defined(GU_STOCK_PROFILE_ORIGINAL_03100062) || defined(GU_STOCK_PROFILE_OBSERVED_03000062) || defined(GU_ENTRY_LATCHED_SS1_PRESERVE) || defined(GU_ENTRY_LATCHED_SS1_FULL)
#error Stock profile selection requires experimental firmware
#endif
#if defined(GU_ENTRY_LATCHED_SS1_PRESERVE) || defined(GU_ENTRY_LATCHED_SS1_FULL)
#define GU_ENTRY_LATCHED 1
#endif
#define GU_SELECTOR_PHASE_ENTRY 0U
#define GU_SELECTOR_PHASE_SS0_WRITTEN 1U
#define GU_SELECTOR_PHASE_SS1_WRITE 2U
#define GU_SELECTOR_PHASE_TARGET 3U
typedef struct GU_DMA GU_DMA;
struct GU_DMA {
  GU_DMA *next;
  VOID *cpu, *mapping;
  UINT64 dma;
  UINTN bytes, pages;
  BOOLEAN mapped;
};
typedef struct {
  EFI_BOOT_SERVICES *bs;
  EFI_PCI_IO_PROTOCOL *pci;
  EFI_HANDLE image, handle;
  UINT64 bar_base, bar_size, ticks;
  UINT16 pm_cap, pcie_cap, msi_cap, msix_cap;
  UINT32 saved_config[64];
  UINT16 saved_devctl, saved_devctl2;
  UINT8 pcie_version;
  BOOLEAN fault, retained, started, quiesced, armed, owned, display_ok, saved;
  BOOLEAN profile_verified, prom_verified;
  BOOLEAN master_attributes_pending;
  UINT64 master_original_attributes;
  UINT16 master_original_command;
  EFI_PCI_IO_PROTOCOL *master_pci;
  /* Containment may run at TPL_NOTIFY. Capture diagnostics without file IO;
     preserve first errors even if a later containment retry succeeds. */
  EFI_STATUS master_attribute_status, master_hardware_status;
  UINT64 master_last_attributes;
  UINT16 master_last_command;
  UINT32 flrs, mmio_writes, dma_count;
  GU_DMA *allocations;
  BOOLEAN (*display_check)(VOID);
} GU_PLATFORM;
extern GU_PLATFORM Gu;

VOID GuInit(EFI_BOOT_SERVICES *Bs, EFI_PCI_IO_PROTOCOL *Pci);
#ifdef GU_ENTRY_LATCHED
/* Only ColdCheck may latch, after all exact-card/cold/PROM checks. These
   values are private to port.c; the vendor core can only read/compare them. */
BOOLEAN GuLatchEntrySelectors(UINT32 Ss0, UINT32 Ss1);
BOOLEAN GuGetEntrySelectors(UINT32 *Ss0, UINT32 *Ss1);
BOOLEAN GuEntrySelectorsMatch(UINT32 Ss0, UINT32 Ss1);
#endif
#ifdef GU_ENTRY_LATCHED_SS1_FULL
/* The full policy owns the only permitted SS1 write phase. */
BOOLEAN GuSelectorPhase(UINT32 Phase);
#endif
BOOLEAN GuTargetConfig(const UINT32 *Config, UINTN Segment, UINTN Bus, UINTN Device, UINTN Function);
BOOLEAN GuSafeToReturn(VOID);
EFI_STATUS GuDmaAllocate(UINTN Bytes, VOID **Cpu, UINT64 *Dma);
EFI_STATUS GuDmaFree(VOID *Cpu, UINT64 Dma);
UINT32 GuRead32(UINT32 Offset);
VOID GuWrite32(UINT32 Offset, UINT32 Value);
EFI_STATUS GuSavePci(VOID);
EFI_STATUS GuRestorePci(VOID);
EFI_STATUS GuFlr(VOID);
EFI_STATUS GuClearMaster(VOID);
EFI_STATUS GuEnableMaster(VOID);
EFI_STATUS GuConfigRead(EFI_PCI_IO_PROTOCOL_WIDTH Width, UINT32 Offset, VOID *Value);
EFI_STATUS GuConfigWrite(EFI_PCI_IO_PROTOCOL_WIDTH Width, UINT32 Offset, VOID *Value);
VOID GuStarted(VOID);
VOID GuQuiesced(VOID);
VOID GuLog(const char *Format, ...);
VOID GuLogText(const char *Text);
VOID GuCopyFromIo(VOID *Destination, const VOID *Source, size_t Bytes);
VOID *GuAlloc(size_t Bytes);
VOID GuFree(VOID *Memory);
VOID GuStall(UINTN Microseconds);
