/* Modified 2026-10-19/20 for Windows GPU UNLOCK; changes documented in CHANGES.md. */ #include "app.h"
#include "app_io.h"
#include "compat/compat.h"
#include "checks.h"
#include "sha256.h"
#include "ownership.h"
#include "vendor/unlock_610.h"
#include "vendor/vbios_610.h"
#include "vendor/gfw_eval.h"
#include <Protocol/GraphicsOutput.h>
#include <IndustryStandard/Acpi.h>
#include <Guid/GlobalVariable.h>
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
  PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,GlobalGuid=EFI_GLOBAL_VARIABLE;
static EFI_SYSTEM_TABLE *Table;
static UINT32 InitialConfig[64];
static const char *Stage;
static GU_RELEASE_OWNER PendingOwner;
static EFI_STATUS Step(EFI_STATUS Status) {
  GuLog("CHECK stage=%s status=%#llx\n",Stage,(unsigned long long)Status);
  return !GuLogReady() && !EFI_ERROR(Status)?EFI_DEVICE_ERROR:Status;
}
static VOID ConfigLog(const UINT32 *Config) {
  for(UINTN i=0;i<64;i+=4)GuLog("config[%02x]=%08x %08x %08x %08x\n",
    (unsigned)(i*4),Config[i],Config[i+1],Config[i+2],Config[i+3]);
}
static VOID GopInfoLog(UINTN Index,const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info) {
  GuLog("GOP[%u] format=%u size=%ux%u stride=%u\n",(unsigned)Index,
    (unsigned)Info->PixelFormat,Info->HorizontalResolution,Info->VerticalResolution,Info->PixelsPerScanLine);
}
static BOOLEAN GopPciParent(EFI_DEVICE_PATH_PROTOCOL *Path,UINT32 *IdOut,UINT32 *SubOut,
    UINTN *SOut,UINTN *BOut,UINTN *DOut,UINTN *FOut) {
  /* Resolve the GOP's PCI ancestor and report identity plus location, so the
     display provider can be proven independent of the unlock target. */
  EFI_DEVICE_PATH_PROTOCOL *remaining=Path;EFI_HANDLE parent=NULL;
  EFI_PCI_IO_PROTOCOL *pci=NULL;UINT32 id=0,subsystem=0;UINTN s,b,d,f;
  if(!Gu.bs->LocateDevicePath)return FALSE;
  EFI_STATUS status=Gu.bs->LocateDevicePath(&PciGuid,&remaining,&parent);
  if(EFI_ERROR(status) || !parent)return FALSE;
  status=Gu.bs->HandleProtocol(parent,&PciGuid,(VOID **)&pci);
  if(EFI_ERROR(status) || !pci || !pci->GetLocation || !pci->Pci.Read)return FALSE;
  status=pci->GetLocation(pci,&s,&b,&d,&f);
  if(EFI_ERROR(status))return FALSE;
  status=pci->Pci.Read(pci,EfiPciIoWidthUint32,0,1,&id);
  if(!EFI_ERROR(status))status=pci->Pci.Read(pci,EfiPciIoWidthUint32,0x2c,1,&subsystem);
  GuLog("GOP PCI parent: status=%#llx id=%#x subsystem=%#x bdf=%u:%u:%u.%u\n",
    (unsigned long long)status,id,subsystem,(unsigned)s,(unsigned)b,(unsigned)d,(unsigned)f);
  if(IdOut)*IdOut=id;if(SubOut)*SubOut=subsystem;
  if(SOut)*SOut=s;if(BOut)*BOut=b;if(DOut)*DOut=d;if(FOut)*FOut=f;
  return !EFI_ERROR(status);
}
static BOOLEAN DisplayCheck(VOID) {
  /* GA107M laptop: the EFI console must come from the Intel iGOP (00:02.0)
   * and the NVIDIA target must expose no physical GOP of its own.
   * HP firmware additionally exposes a pathless console-wrapper GOP
   * (ConSplitter-style aggregator) carrying the same framebuffer and mode
   * as the physical Intel GOP; classify such handles as mirrors. */
  EFI_HANDLE *handles=NULL;UINTN count=0,intel=0,nvidia=0,other=0,mirror=0,pathless=0;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *physical[16];
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *unresolved[16];
  EFI_STATUS status=Gu.bs->LocateHandleBuffer(ByProtocol,&GopGuid,NULL,&count,&handles);
  GuLog("GOP enumeration: status=%#llx handles=%zu\n",(unsigned long long)status,(size_t)count);
  if(EFI_ERROR(status) || !handles || !count || count>16){if(handles)GuFree(handles);
#if GU_PREFLIGHT_ONLY || GU_PROBE_MODE
    GuLogText("preflight: GOP enumeration unusable; observation continues\n");return GuLogReady();
#else
    return FALSE;
#endif
  }
  for(UINTN i=0;i<count;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_DEVICE_PATH_PROTOCOL *path=NULL;
    status=Gu.bs->HandleProtocol(handles[i],&GopGuid,(VOID **)&gop);
    if(EFI_ERROR(status) || !gop || !gop->Mode){
      GuLog("GOP[%u] invalid protocol/mode: status=%#llx\n",(unsigned)i,
        (unsigned long long)status);other++;continue;
    }
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode=gop->Mode;
    status=Gu.bs->HandleProtocol(handles[i],&PathGuid,(VOID **)&path);
    GuLog("GOP[%u] path_status=%#llx framebuffer=%#llx bytes=%zu mode=%u max_mode=%u info_bytes=%zu console=%u\n",
      (unsigned)i,(unsigned long long)status,(unsigned long long)mode->FrameBufferBase,
      (size_t)mode->FrameBufferSize,mode->Mode,mode->MaxMode,(size_t)mode->SizeOfInfo,
      (unsigned)(handles[i]==Table->ConsoleOutHandle));
    if(mode->Info && mode->SizeOfInfo>=sizeof(*mode->Info))GopInfoLog(i,mode->Info);
    if(EFI_ERROR(status) || !path){
      /* Pathless handle: defer classification; it may be a console mirror. */
      GuLog("GOP[%u] pathless; deferred classification\n",(unsigned)i);
      if(pathless<16)unresolved[pathless++]=mode;else other++;
      continue;
    }
    UINT32 id=0,sub=0;UINTN s,b,d,f;
    if(!GopPciParent(path,&id,&sub,&s,&b,&d,&f)){GuLog("GOP[%u] PCI parent unresolved\n",(unsigned)i);other++;continue;}
    if(id==GU_IGPU_ID && sub==GU_IGPU_SUBSYSTEM && !s && b==0 && d==2 && !f) {
      if(!mode->FrameBufferBase || !mode->FrameBufferSize){GuLog("GOP[%u] iGPU GOP without framebuffer\n",(unsigned)i);other++;continue;}
      physical[intel]=mode;intel++;
    } else if(id==GU_BOARD_ID) {
      GuLog("GOP[%u] belongs to the NVIDIA unlock target\n",(unsigned)i);
      nvidia++;
    } else other++;
  }
  /* Classify deferred pathless handles: a console wrapper mirrors the single
   * physical Intel GOP exactly (framebuffer, mode index, mode info). */
  for(UINTN i=0;i<pathless;i++) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *m=unresolved[i];BOOLEAN ok=FALSE;
    if(intel==1 && m->FrameBufferBase==physical[0]->FrameBufferBase &&
       m->FrameBufferSize==physical[0]->FrameBufferSize && m->Mode==physical[0]->Mode &&
       m->SizeOfInfo==physical[0]->SizeOfInfo && m->Info && physical[0]->Info) {
      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *a=m->Info,*p=physical[0]->Info;
      if(m->SizeOfInfo>=sizeof(*a) && a->Version==p->Version && a->PixelFormat==p->PixelFormat &&
         a->HorizontalResolution==p->HorizontalResolution && a->VerticalResolution==p->VerticalResolution &&
         a->PixelsPerScanLine==p->PixelsPerScanLine)ok=TRUE;
    }
    if(ok){mirror++;GuLog("GOP pathless handle %zu classified console mirror\n",(size_t)i);}
    else other++;
  }
  GuFree(handles);GuLog("GOP result: intel=%zu nvidia=%zu mirror=%zu other=%zu\n",
    (size_t)intel,(size_t)nvidia,(size_t)mirror,(size_t)other);
#if GU_PROBE_MODE
  /* Probe: same inventory policy as preflight - classification is recorded,
     never a gate. The strict one-Intel-GOP/zero-other topology is enforced
     only by the experimental build. */
  return GuLogReady();
#elif GU_PREFLIGHT_ONLY
  /* Inventory build: classification is recorded for the report, never a gate. */
  return GuLogReady();
#else
  /* Experimental: exactly one physical Intel GOP, none on the target, and
     every pathless handle must be an exact mirror of the physical GOP. */
  return intel==1 && nvidia==0 && other==0 && mirror==(size_t)pathless && GuLogReady();
#endif
}
static EFI_STATUS ReadConfig(UINT32 *Config) {
  for(UINTN i=0;i<64;i++)if(EFI_ERROR(GuConfigRead(EfiPciIoWidthUint32,(UINT32)i*4,&Config[i]))) {
    GuLog("PCI config read failed: offset=%#x valid_bytes=%u; snapshot incomplete\n",(unsigned)(i*4),(unsigned)(i*4));
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS ConfigCheck(BOOLEAN Save) {
  UINT32 config[64];GU_CAPS caps;UINTN segment,bus,device,function;
  EFI_STATUS status=Gu.pci->GetLocation(Gu.pci,&segment,&bus,&device,&function);
  GuLog("target location: status=%#llx\n",(unsigned long long)status);
  if(EFI_ERROR(status))return status;
  status=ReadConfig(config);if(EFI_ERROR(status))return status;
  BOOLEAN checked=GuCheckConfig(config,segment,bus,device,function,&caps);
  /* GA107M laptop: class is 0x030200 (VGA-compatible controller, not 0x030000).
   * BAR0 base is assigned by firmware each boot and latched from the
   * descriptor in BarCheck, so it is not pinned as a config dword here.
   * caps.pcie is only valid when the structural parse succeeded. */
  BOOLEAN valid=checked && config[2]==0x030200a1 &&
    config[GU_EXPECTED_CONFIG_CAP_OFFSET/4]==GU_EXPECTED_CONFIG_CAP_VALUE &&
    !(config[(caps.pcie+8)/4]&0x00200000U);
  if(checked)GuLog("config profile: class_pin=%u cap_dword_pin=%u devctl_relaxed=%u\n",
    (unsigned)(config[2]==0x030200a1),
    (unsigned)(config[GU_EXPECTED_CONFIG_CAP_OFFSET/4]==GU_EXPECTED_CONFIG_CAP_VALUE),
    (unsigned)!!(config[(caps.pcie+8)/4]&0x00200000U));
#if GU_PREFLIGHT_ONLY
  /* HP laptop firmware exposes the dGPU with PCI memory decode DISABLED
     before the driver binds: command register reads 0x0000 (no MEM/IO/BME),
     so GuCheckConfig (which gates writes and requires decode enabled)
     correctly reports checks=0, and PciIo Mem reads return erased-flash
     values (0xffffffff / all-FF PROM). That is the expected cold state on
     this machine, not an anomaly. Record the classification for the report. */
  UINT16 cmd=(UINT16)(config[1]&0xffffU);
  BOOLEAN cold_hidden=config[0]==GU_BOARD_ID &&
    config[0x2c/4]==GU_BOARD_SUBSYSTEM &&
    (config[2]>>24)==3 && (config[2]&0xff)==0x02 && config[2]==0x030200a1 &&
    ((config[3]>>16)&0x7fU)==0 && (cmd&0x0002U)==0;
  GuLog("preflight cold decode classification: hidden=%u command=%#x\n",
    (unsigned)(cold_hidden?1:0),(unsigned)cmd);
  if(cold_hidden)
    GuLogText("preflight: dGPU decode disabled (cold); register/PROM reads will be erased-flash values until the experimental build enables decode itself\n");
#endif
  BOOLEAN same=Save || !memcmp(config,InitialConfig,sizeof(config));
  if(Save || !valid || !same)ConfigLog(config);
  GuLog("target BDF=%zu:%zu:%zu.%zu checks=%u pm=%#x pcie=%#x msi=%#x msix=%#x\n",
    (size_t)segment,(size_t)bus,(size_t)device,(size_t)function,checked,
    (unsigned)caps.pm,(unsigned)caps.pcie,(unsigned)caps.msi,(unsigned)caps.msix);
#if GU_PREFLIGHT_ONLY
  /* Inventory build: record the full config and pin matches; never gate. */
  if(!checked)GuLogText("preflight: config structural parse failed; observation continues\n");
  if(!valid)GuLogText("preflight: config profile pins not matched yet; observation continues\n");
  if(Save)memcpy(InitialConfig,config,sizeof(config));
  if(checked){Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;}
  return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
#elif GU_PROBE_MODE
  /* Probe: identical inventory recording, then latch the config snapshot
     (EnableDecode compares against it) and continue to the decode step. */
  if(Save)memcpy(InitialConfig,config,sizeof(config));
  if(checked){Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;}
  return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
#elif defined(GU_EXPERIMENTAL_3090)
  /* Experimental: two-phase validation for this firmware.
   *
   * Save=TRUE (first contact, decode still off - proven by RUN00-03):
   *   accept ONLY the exact cold decode-off identity (board ID, subsystem,
   *   class) and latch the snapshot for EnableDecode's side-effect
   *   comparison. A decode-enabled card at this stage is refused.
   *
   * Save=FALSE (config-strict, after decode-enable): run the full strict
   *   validation - structural parse, pinned dwords - and compare the live
   *   config against the post-decode latch, skipping dword 32 (PCIe DEVCNTL
   *   + DEVSTS): the card raises DEVSTS status bits on its own as PCIe
   *   traffic flows (observed twice on this machine), which is hardware
   *   status, not config drift. Any other differing dword fails closed
   *   with its exact offset logged. */
  if(Save) {
    if(config[0]!=GU_BOARD_ID || config[0x2c/4]!=GU_BOARD_SUBSYSTEM ||
       config[2]!=0x030200a1)return EFI_UNSUPPORTED;
    memcpy(InitialConfig,config,sizeof(InitialConfig));
    return EFI_SUCCESS;
  }
  if(!valid)return EFI_UNSUPPORTED;
  for(UINTN i=0;i<64;i++) {
    if(i==32)continue;
    if(config[i]!=InitialConfig[i]){
      GuLog("config drift at dword %u: latched=%#x now=%#x\n",
        (unsigned)i,InitialConfig[i],config[i]);
      return EFI_NOT_READY;
    }
  }
  Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;
  return EFI_SUCCESS;
#else
  if(!valid)return EFI_UNSUPPORTED;
  if(!same)return EFI_NOT_READY;
  if(Save)memcpy(InitialConfig,config,sizeof(config));
  Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;
  return EFI_SUCCESS;
#endif
}
static EFI_STATUS RestoreDecode(VOID) {
  UINT32 current[64],normalized[64],after[64];UINTN segment,bus,device,function;
  if(GU_PREFLIGHT_ONLY || !Gu.profile_verified || !Gu.pci || !Gu.owned || !Gu.armed || !Gu.display_ok || Gu.started || Gu.allocations ||
    Gu.dma_count || Gu.fault || Gu.retained)return EFI_ACCESS_DENIED;
  EFI_STATUS status=Gu.pci->GetLocation(Gu.pci,&segment,&bus,&device,&function);
  if(EFI_ERROR(status))return status;
  status=ReadConfig(current);if(EFI_ERROR(status))return status;
  GuLogText("config before decode restore (256 valid bytes):\n");ConfigLog(current);
  if(!GuTargetConfig(current,segment,bus,device,function))return EFI_UNSUPPORTED;
  UINT32 missing=InitialConfig[1]&~current[1]&3U;
  memcpy(normalized,current,sizeof(normalized));normalized[1]|=missing;
  /* Restore only cleared, previously enabled IO/MEM bits. Every other bit,
     including BME, status, BAR, ROM and PM/capability state must be unchanged. */
  if(memcmp(normalized,InitialConfig,sizeof(normalized))) {
    GuLogText("decode restore refused: config differs beyond cleared original IO/MEM bits\n");
    return EFI_UNSUPPORTED;
  }
  if(!missing){GuLogText("decode restore not needed\n");return EFI_SUCCESS;}
  if(!Gu.pci->Attributes)return EFI_UNSUPPORTED;
  UINT64 needed=(missing&1?EFI_PCI_IO_ATTRIBUTE_IO:0)|(missing&2?EFI_PCI_IO_ATTRIBUTE_MEMORY:0);
  UINT64 supported=0,attributes=0;
  status=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationSupported,0,&supported);
  GuLog("decode supported attributes: status=%#llx mask=%#llx\n",(unsigned long long)status,(unsigned long long)supported);
  if(EFI_ERROR(status))return status;
  if((supported&needed)!=needed)return EFI_UNSUPPORTED;
  status=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attributes);
  GuLog("decode current attributes: status=%#llx mask=%#llx\n",(unsigned long long)status,(unsigned long long)attributes);
  if(EFI_ERROR(status))return status;
  GuLog("decode restore intent: saved_command=%#x current_command=%#x missing=%#x attributes=%#llx\n",
    InitialConfig[1]&0xffff,current[1]&0xffff,missing,(unsigned long long)needed);
  if(!GuLogReady() || Gu.fault)return EFI_DEVICE_ERROR;
  /* Never use EFI_PCI_DEVICE_ENABLE: it would also enable bus mastering. */
  EFI_STATUS enabled=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationEnable,needed,NULL);
  GuLog("decode enable status=%#llx\n",(unsigned long long)enabled);
  if(!GuLogReady() || Gu.fault)return EFI_ERROR(enabled)?enabled:EFI_DEVICE_ERROR;
  /* Even an error may follow a partial hardware effect. Capture it if readable,
     but never turn an API error into success merely because readback matches. */
  status=ReadConfig(after);
  BOOLEAN matched=FALSE;
  if(!EFI_ERROR(status)) {
    GuLogText("config after decode restore (256 valid bytes):\n");ConfigLog(after);
    matched=!memcmp(after,InitialConfig,sizeof(after));GuLog("decode readback matched=%u\n",matched);
  }
  if(EFI_ERROR(enabled))return enabled;
  if(EFI_ERROR(status))return status;
  if(!GuLogReady() || Gu.fault)return EFI_DEVICE_ERROR;
  return matched?EFI_SUCCESS:EFI_COMPROMISED_DATA;
}
#if GU_PREFLIGHT_ONLY
/* Decode-enable path exists only in experimental/probe builds; the plain
   preflight performs no config writes of any kind. */
#elif GU_PROBE_MODE || defined(GU_EXPERIMENTAL_3090)
/* PCI command register constants (app-local; checks.c keeps its own copies). */
#define GU_CMD_MEM 0x0002U
#define GU_CMD_BME 0x0004U
static UINT16 CmdWord(const UINT32 Config[64]) { return (UINT16)(Config[1] & 0xffffU); }
static EFI_STATUS EnableDecode(VOID) {
  /* Probe/experimental: enable only the MEM decode bit so cold registers
     and the real PROM become readable. This is the identical config write
     the Windows driver performs on every boot. In probe builds nothing
     else follows; in experimental builds it precedes cold verification.
     No IO, no bus master, no VGAsave, no ROM bit is touched. */
#if GU_PROBE_MODE
  GuLogText("PROBE: enabling MEM decode only; reads follow; no MMIO/DMA/firmware/selector writes.\n");
#else
  GuLogText("Enabling MEM decode for cold-state verification (driver-equivalent config write).\n");
#endif
  UINT32 current[64],after[64];UINTN segment,bus,device,function;
  if(!Gu.pci)return EFI_DEVICE_ERROR;
  EFI_STATUS status=Gu.pci->GetLocation(Gu.pci,&segment,&bus,&device,&function);
  if(EFI_ERROR(status))return status;
  status=ReadConfig(current);if(EFI_ERROR(status))return status;
  UINT16 cmd=CmdWord(current);
  GuLog("decode-enable intent: command=%#x mem_bit=%u\n",cmd,cmd&GU_CMD_MEM?1:0);
  if(cmd&GU_CMD_MEM){GuLogText("decode already enabled\n");return EFI_SUCCESS;}
  if(!Gu.pci->Attributes)return EFI_UNSUPPORTED;
  UINT64 supported=0,attributes=0;
  status=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationSupported,0,&supported);
  if(EFI_ERROR(status))return status;
  if(!(supported&EFI_PCI_IO_ATTRIBUTE_MEMORY))return EFI_UNSUPPORTED;
  status=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationGet,0,&attributes);
  if(EFI_ERROR(status))return status;
  GuLog("decode enable attributes: supported=%#llx current=%#llx\n",
    (unsigned long long)supported,(unsigned long long)attributes);
  /* Enable memory decode only: no IO, no bus master, no VGAsave, no ROM. */
  status=Gu.pci->Attributes(Gu.pci,EfiPciIoAttributeOperationEnable,
    EFI_PCI_IO_ATTRIBUTE_MEMORY,NULL);
  if(EFI_ERROR(status))return status;
  status=ReadConfig(after);if(EFI_ERROR(status))return status;
  UINT16 after_cmd=CmdWord(after);
  BOOLEAN cmd_ok=(after_cmd&GU_CMD_MEM)!=0 && (after_cmd&GU_CMD_BME)==0;
  GuLog("decode enable result: command=%#x mem=%u bme=%u\n",
    after_cmd,after_cmd&GU_CMD_MEM?1:0,after_cmd&GU_CMD_BME?1:0);
  if(!cmd_ok){GuLogText("decode enable failed readback\n");return EFI_DEVICE_ERROR;}
  /* PciIo's own enable covers the command register; allow exactly MEM.
   * dword 32 (offset 0x80) is PCIe DEVCNTL (low 16) + DEVSTS (high 16).
   * RUN02 observed DEVSTS gaining bits 0|3 (Correctable Error Detected /
   * Unsupported Request Detected) the moment decode turned on - read-only
   * status bits the device raises itself. DEVCNTL itself must not change. */
  for(UINTN i=0;i<64;i++) {
    if(i==1||i==32)continue;
    if(after[i]!=InitialConfig[i]){GuLog("decode side effect at dword %u: saved=%#x now=%#x\n",
      (unsigned)i,InitialConfig[i],after[i]);return EFI_DEVICE_ERROR;}
  }
  if((after[32]^InitialConfig[32])&0xffffU){
    GuLog("decode changed DEVCNTL: saved=%#x now=%#x\n",
      InitialConfig[32]&0xffff,after[32]&0xffff);return EFI_DEVICE_ERROR;
  }
  if((after[32]^InitialConfig[32])>>16)
    GuLog("DEVSTS delta observed (hardware status): saved=%#x now=%#x\n",
      (InitialConfig[32]>>16)&0xffff,(after[32]>>16)&0xffff);
  if((((InitialConfig[1]&0xffff)^after_cmd)&~GU_CMD_MEM)){
    GuLog("decode changed command bits beyond MEM: saved=%#x now=%#x\n",
      InitialConfig[1]&0xffff,after_cmd);return EFI_DEVICE_ERROR;
  }
  memcpy(InitialConfig,after,sizeof(InitialConfig));
  return EFI_SUCCESS;
}
#endif
static EFI_STATUS FindTarget(VOID) {
  EFI_HANDLE *handles=NULL;UINTN count=0,found=0;
  EFI_STATUS status=Gu.bs->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&count,&handles);
  GuLog("PCI enumeration: status=%#llx handles=%zu\n",(unsigned long long)status,(size_t)count);
  if(EFI_ERROR(status) || !handles || !count || count>256){if(handles)GuFree(handles);return EFI_NOT_FOUND;}
  for(UINTN i=0;i<count;i++) {
    EFI_PCI_IO_PROTOCOL *pci=NULL;UINT32 id=0;
    status=Gu.bs->HandleProtocol(handles[i],&PciGuid,(VOID **)&pci);
    if(!EFI_ERROR(status) && pci)status=pci->Pci.Read(pci,EfiPciIoWidthUint32,0,1,&id);
    if(EFI_ERROR(status) || !pci){GuLog("PCI scan failed: index=%zu status=%#llx\n",(size_t)i,(unsigned long long)status);status=EFI_DEVICE_ERROR;break;}
    if(id==GU_BOARD_ID){found++;Gu.handle=handles[i];Gu.pci=pci;}
  }
  GuFree(handles);GuLog("PCI target matches=%zu status=%#llx\n",(size_t)found,(unsigned long long)status);
  if(EFI_ERROR(status) || found!=1)return EFI_UNSUPPORTED;
  return ConfigCheck(TRUE);
}
static EFI_STATUS BarCheck(VOID) {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *r=NULL;
  EFI_STATUS status=Gu.pci->GetBarAttributes(Gu.pci,0,NULL,(VOID **)&r);
  GuLog("bar0 query: status=%#llx\n",(unsigned long long)status);
  if(!EFI_ERROR(status)) {
    if(r && r->Desc==ACPI_ADDRESS_SPACE_DESCRIPTOR && r->Len==sizeof(*r)-3)
      GuLog("bar0 descriptor: type=%u granularity=%llu min=%#llx max=%#llx length=%#llx translation=%#llx\n",
        (unsigned)r->ResType,(unsigned long long)r->AddrSpaceGranularity,(unsigned long long)r->AddrRangeMin,
        (unsigned long long)r->AddrRangeMax,(unsigned long long)r->AddrLen,(unsigned long long)r->AddrTranslationOffset);
    if(!r || r->Desc!=ACPI_ADDRESS_SPACE_DESCRIPTOR || r->Len!=sizeof(*r)-3 || r->ResType!=0 ||
      r->AddrLen!=0x1000000 || r->AddrTranslationOffset || r->AddrSpaceGranularity!=32 ||
      ((UINT8 *)(r+1))[0]!=ACPI_END_TAG_DESCRIPTOR)status=EFI_UNSUPPORTED;
    else {
      /* Validate the descriptor checksum, then latch this boot's BAR0 window.
         The base is firmware-assigned; every later MMIO/PROM access goes
         through Gu.bar_base/Gu.bar_size. */
      UINT8 *raw=(UINT8 *)r,sum=0;
      for(UINTN i=0;i<sizeof(*r)+2;i++)sum=(UINT8)(sum+raw[i]);
      if(raw[sizeof(*r)+1] && sum)status=EFI_COMPROMISED_DATA;
      else {Gu.bar_base=r->AddrRangeMin;Gu.bar_size=r->AddrLen;}
    }
  }
  if(r)GuFree(r);
#if GU_PREFLIGHT_ONLY
  /* UEFI assigns BAR0 dynamically; record the value this boot for pinning. */
  GuLog("bar0 observed: base=%#llx size=%#llx pinned=%#llx match=%u\n",
    (unsigned long long)Gu.bar_base,(unsigned long long)Gu.bar_size,
    (unsigned long long)GU_BAR0_BASE,(unsigned)(Gu.bar_base==GU_BAR0_BASE));
  return EFI_ERROR(status)?status:(GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR);
#else
  /* The exact-card pin is the BAR0 length/alignment class (16 MiB, 32-bit,
     type 0), already enforced above; the base is assigned by firmware and
     latched from the descriptor, so all later MMIO uses Gu.bar_base. */
  if(!EFI_ERROR(status))GuLog("bar0 latched: base=%#llx size=%#llx\n",
    (unsigned long long)Gu.bar_base,(unsigned long long)Gu.bar_size);
  return status;
#endif
}
static EFI_STATUS ColdCheck(VOID) {
  Gu.profile_verified=FALSE;
  /* Cold-start observation: this HP firmware leaves the dGPU with PCI memory
     decode disabled, so PciIo Mem.Read returns garbage (0xffffffff) until the
     command register's MEM bit is enabled. The preflight never writes; it
     reads via PciIo, whose Mem access on EDK2 covers the BAR even when the
     device's own decode bit is clear. Record both facts and only report the
     raw register data. */
  UINT32 command=0;
  if(Gu.pci && EFI_ERROR(GuConfigRead(EfiPciIoWidthUint32,4,&command)))
    GuLog("cold command read failed\n");
  UINT32 chip=GuRead32(0);
  struct gpu_unlock_gfw_sample s={.config_ok=true,.plm=GuRead32(GU_GFW_PLM_OFFSET),
    .boot=GuRead32(GU_GFW_BOOT_OFFSET),.cpuctl=GuRead32(GU_GFW_CPUCTL_OFFSET)};
  UINT32 hi=GuRead32(0x1fa828),plm=GuRead32(GU_FEAT_PLM_OFFSET),ss0=GuRead32(GU_FEAT_SS0_OFFSET),ss1=GuRead32(GU_FEAT_SS1_OFFSET);
  GuLog("cold state: command=%#x chip=%#x plm=%#x gfw=%#x/%#x/%#x WPR2_HI=%#x SS0=%#x SS1=%#x\n",
    command,chip,plm,s.plm,s.boot,s.cpuctl,hi,ss0,ss1);
  UINT32 memory=GuRead32(0x1183a4);
  if(chip==0xffffffffU)
    GuLogText("cold registers read 0xffffffff: dGPU decode is off (command MEM bit clear) or clocks are down; expected on this firmware before the driver binds\n");
#if !defined(GU_EXPERIMENTAL_3090)
  GuLog("DIAGNOSTIC PREFLIGHT ONLY: cannot arm; no GPU writes/reset/DMA/driver release\n");
  GuLog("preflight pins for profile transcription: chip=%#x gfw_plm=%#x gfw_boot=%#x gfw_cpuctl=%#x wpr2_hi=%#x feat_plm=%#x memory=%#x ss0=%#x ss1=%#x\n",
    chip,s.plm,s.boot,s.cpuctl,hi,plm,memory,ss0,ss1);
  return Gu.fault?EFI_DEVICE_ERROR:EFI_NOT_READY;
#else
#ifdef GU_ENTRY_LATCHED
  UINT32 entry0=0,entry1=0;BOOLEAN latched=GuGetEntrySelectors(&entry0,&entry1);
  GuLog("cold expected: profile=%s chip=%#x memory=%#x entry_latched=%u SS0=%#x SS1=%#x SS0target=0x88888888 SS1policy=%s entry=%#x target=0x8\n",
    GU_STOCK_PROFILE_NAME,GU_EXPECTED_CHIP,GU_DEVICE_MEMORY_SIZE,latched,entry0,entry1,
#ifdef GU_ENTRY_LATCHED_SS1_PRESERVE
    "preserve",
#else
    "target",
#endif
    entry1);
#else
  GuLog("cold expected: profile=%s chip=%#x memory=%#x SS0=%#x SS1=%#x\n",
    GU_STOCK_PROFILE_NAME,GU_EXPECTED_CHIP,GU_DEVICE_MEMORY_SIZE,GU_STOCK_PROFILE_SS0,6U);
#endif
  GuLog("cold observed: profile=%s chip=%#x memory=%#x SS0=%#x SS1=%#x\n",
    GU_STOCK_PROFILE_NAME,chip,memory,ss0,ss1);
  if(Gu.fault)return EFI_DEVICE_ERROR;
  if(chip!=GU_EXPECTED_CHIP || memory!=GU_DEVICE_MEMORY_SIZE ||
     gpu_unlock_gfw_assess(&s) || s.plm!=GU_EXPECTED_GFW_PLM || s.boot!=GU_EXPECTED_GFW_BOOT ||
     s.cpuctl!=GU_EXPECTED_GFW_CPUCTL || hi!=GU_EXPECTED_WPR2_HI || plm!=GU_EXPECTED_FEAT_PLM ||
     !Gu.prom_verified)
    return EFI_NOT_READY;
#ifdef GU_ENTRY_LATCHED
  if(!GuLatchEntrySelectors(ss0,ss1))return EFI_NOT_READY;
  GuLog("entry selectors: latched_once=1 SS0=%#x SS1=%#x SS0target=0x88888888 SS1policy=%s SS1target=0x8\n",ss0,ss1,
#ifdef GU_ENTRY_LATCHED_SS1_PRESERVE
    "preserve",
#else
    "full",
#endif
    ss1);
  /* Logging is external I/O: a failed log never authorizes this profile. */
  if(!GuLogReady() || Gu.fault)return EFI_DEVICE_ERROR;
#else
  if(ss0!=GU_STOCK_PROFILE_SS0 || ss1!=6)return EFI_NOT_READY;
#endif
  Gu.profile_verified=TRUE;
#ifdef GU_ENTRY_LATCHED
  GuLog("EXPERIMENTAL exact-card profile %s verified; SS0target=0x88888888 SS1policy=%s entry=%#x target=0x8; hardware untested.\n",GU_STOCK_PROFILE_NAME,
#ifdef GU_ENTRY_LATCHED_SS1_PRESERVE
    "preserve",
#else
    "full",
#endif
    ss1);
#else
  GuLog("EXPERIMENTAL exact-card profile %s verified; SS1 target 8 effect remains hardware untested.\n",GU_STOCK_PROFILE_NAME);
#endif
  return EFI_SUCCESS;
#endif
}
static EFI_STATUS PromCheck(VOID) {
  Gu.prom_verified=FALSE;
  Gu.profile_verified=FALSE;
  VOID *prom=GuAlloc(1048576);if(!prom)return EFI_OUT_OF_RESOURCES;
  GuCopyFromIo(prom,(VOID *)(UINTN)(Gu.bar_base+0x300000),1048576);
  if(Gu.fault){GuFree(prom);return EFI_DEVICE_ERROR;}
  UINT8 digest[32];
  GuSha256(prom,1048576,digest);
  static const char digits[]="0123456789abcdef";
  char hex[65];
  for(UINTN i=0;i<32;i++){hex[i*2]=digits[digest[i]>>4];hex[i*2+1]=digits[digest[i]&15];}
  hex[64]=0;
  GuLog("PROM sha256=%s\n",hex);
#if GU_PREFLIGHT_ONLY || GU_PROBE_MODE
  /* Inventory: dump the ROM and parse the FWSEC descriptor for the report.
     No hash pin gates these builds; transcribe this RUN's hash into port.h
     afterwards. The probe's post-decode run overwrites the pre-decode
     all-FF dump with the real ROM. */
  GuLog("profile pin expected: %s\n",GU_CARD_PROFILE_PROM_SHA256);
  EFI_STATUS dumped=GuFileDumpInRun(L"PROM.BIN","PROM.BIN",prom,1048576);
  struct gpu_unlock_fwsec fw;
  int ret=gpu_unlock_vbios_fwsec(prom,1048576,&fw);
  if(ret)GuLog("FWSEC parse: not found in PROM dump (%d)\n",ret);
  else {
    u32 transfer=0;
    int dret=gpu_unlock_fwsec_dmem_transfer(&fw,&transfer);
    GuLog("FWSEC descriptor: image_off=%#x image_size=%#x imem_pa=%#x imem_size=%#x imem_va=%#x dmem_pa=%#x data_off=%#x dmem_size=%#x pkc_off=%#x interface_off=%#x ucode_id=%#x engine_mask=%#x sig_count=%#x sig_versions=%#x sig_off=%#x sig_bytes=%#x dmem_transfer=%#x ret=%d\n",
      fw.image_off,fw.image_size,fw.imem_pa,fw.imem_size,fw.imem_va,fw.dmem_pa,fw.data_off,
      fw.dmem_size,fw.pkc_off,fw.interface_off,fw.ucode_id,fw.engine_mask,fw.sig_count,
      fw.sig_versions,fw.sig_off,fw.sig_bytes,transfer,dret);
  }
  GuFree(prom);
  if(EFI_ERROR(dumped))return dumped;
  return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
#else
  BOOLEAN good=GuHashMatches(prom,1048576,GU_CARD_PROFILE_PROM_SHA256);
  GuFree(prom);Gu.prom_verified=good;GuLog("live PROM hash matched=%u\n",(unsigned)good);
  return good?EFI_SUCCESS:EFI_CRC_ERROR;
#endif
}
static EFI_STATUS Arm(VOID) {
#if !defined(GU_EXPERIMENTAL_3090)
  return EFI_UNSUPPORTED;
#else
  if(!Gu.profile_verified || Gu.fault || !Gu.display_ok)return EFI_ACCESS_DENIED;
  if(!Table->ConIn || !Table->ConIn->Reset || !Table->ConIn->ReadKeyStroke)return EFI_UNSUPPORTED;
  EFI_STATUS status=Table->ConIn->Reset(Table->ConIn,FALSE);if(EFI_ERROR(status))return status;
  GuLog("EXPERIMENTAL MANUAL UNLOCK: profile=%s exact GA107M RTX 3050 Ti 4GB / Intel display / verified PROM and assets.\n",GU_STOCK_PROFILE_NAME);
  if(PendingOwner.agent)GuLogText("U will release the RTX firmware driver, restore verified access settings if needed, then recheck and attempt once.\n");
  GuLogText("Press U to attempt once; Esc or 30-second timeout cancels.\n");
  if(!GuLogReady())return EFI_DEVICE_ERROR;
  for(UINTN i=0;i<300;i++) {
    EFI_INPUT_KEY key;status=Table->ConIn->ReadKeyStroke(Table->ConIn,&key);
    if(!EFI_ERROR(status)) {
      if(key.ScanCode==0x17 || key.UnicodeChar==27)return EFI_ABORTED;
      if(key.UnicodeChar=='u' || key.UnicodeChar=='U')return Gu.profile_verified && !Gu.fault?EFI_SUCCESS:EFI_ACCESS_DENIED;
    } else if(status!=EFI_NOT_READY)return status;
    GuStall(100000);
  }
  return EFI_TIMEOUT;
#endif
}
static VOID Halt(VOID) {
  Gu.retained=TRUE;
  GuLogText("STOP: cleanup cannot be proven. Resources remain reserved.\nPOWER OFF using the physical power button; do not continue to Windows.\n");
  /* No ResetSystem, ExitBootServices, retry, protocol close or memory release. */
  for(;;)Gu.bs->Stall(1000000);
}
static VOID LogContainment(VOID) {
  /* Called only after any TPL_NOTIFY containment section has ended. */
  GuLog("BME containment: attribute_status=%#llx hardware_status=%#llx command=%#x cached=%#llx pending=%u\n",
    (unsigned long long)Gu.master_attribute_status,(unsigned long long)Gu.master_hardware_status,
    Gu.master_last_command,(unsigned long long)Gu.master_last_attributes,Gu.master_attributes_pending);
}
static VOID ReleaseHalt(EFI_STATUS Status,BOOLEAN Attempted) {
  /* A failed claim may follow interface removal/replacement during logging.
     Resolve, validate and clear BME without a callback window or stale access. */
  EFI_TPL old=Gu.bs->RaiseTPL(TPL_NOTIFY);
  EFI_STATUS refreshed=GuRefreshTargetPci();
  EFI_STATUS contained=EFI_ERROR(refreshed)?refreshed:GuClearMaster();
  Gu.bs->RestoreTPL(old);
  LogContainment();
  GuLog("FINAL status=%#llx stage=%s attempted=%u driver_release=1 clean=0 containment=%#llx pci_available=%u\n",
    (unsigned long long)Status,Stage,Attempted,(unsigned long long)contained,(unsigned)(Gu.pci!=NULL));
  Halt();
}
EFI_STATUS EFIAPI efi_main(EFI_HANDLE Image,EFI_SYSTEM_TABLE *St) {
  if(!St || !St->BootServices || !St->RuntimeServices)return EFI_INVALID_PARAMETER;
  Table=St;GuInit(St->BootServices,NULL);Gu.image=Image;Gu.display_check=DisplayCheck;
  memset(&PendingOwner,0,sizeof(PendingOwner));
  Stage="usb-log";EFI_STATUS status=GuIoOpen(Image,St);BOOLEAN attempted=FALSE,release_attempted=FALSE;
  if(EFI_ERROR(status))goto finish;
#if defined(GU_EXPERIMENTAL_3090)
  GuLog("BUILD MODE: EXPERIMENTAL MANUAL UNLOCK; compile-time profile=%s. The shared I/O preflight banner above is superseded: exact-card checks and manual U can authorize one stock transaction.\n",GU_STOCK_PROFILE_NAME);
#endif
  UINT8 secure=0xff;UINTN bytes=1;
  Stage="secure-boot";
  status=St->RuntimeServices->GetVariable(L"SecureBoot",&GlobalGuid,NULL,&bytes,&secure);
  if(EFI_ERROR(status) || bytes!=1 || secure){status=EFI_SECURITY_VIOLATION;goto finish;}
  status=Step(status);if(EFI_ERROR(status))goto finish;
  Stage="watchdog";status=Step(Gu.bs->SetWatchdogTimer(0,0,0,NULL));if(EFI_ERROR(status))goto finish;
  Stage="target";status=Step(FindTarget());if(EFI_ERROR(status))goto finish;
  Stage="display";Gu.display_ok=DisplayCheck();status=Step(Gu.display_ok?EFI_SUCCESS:EFI_UNSUPPORTED);if(EFI_ERROR(status))goto finish;
  /* Read-only validation precedes ownership, asset loading and manual input. */
  Stage="bar0";status=Step(BarCheck());if(EFI_ERROR(status))goto finish;
#if GU_PROBE_MODE
  Stage="prom";status=Step(PromCheck());if(EFI_ERROR(status))goto finish; /* pre-decode: erased-flash observation */
  Stage="diagnostic-cold-state";status=Step(ColdCheck());
  if(status==EFI_NOT_READY)status=EFI_SUCCESS; /* pre-decode inventory collected */
  if(EFI_ERROR(status))goto finish;
  Stage="decode-enable";status=Step(EnableDecode());if(EFI_ERROR(status))goto finish;
  Stage="bar0-after-decode";status=Step(BarCheck());if(EFI_ERROR(status))goto finish;
  Stage="prom-after-decode";status=Step(PromCheck());if(EFI_ERROR(status))goto finish;
  Stage="cold-after-decode";status=Step(ColdCheck());
  if(status==EFI_NOT_READY)status=EFI_SUCCESS; /* inventory collected */
  goto finish;
#elif !defined(GU_EXPERIMENTAL_3090)
  Stage="prom";status=Step(PromCheck());if(EFI_ERROR(status))goto finish;
  Stage="diagnostic-cold-state";status=Step(ColdCheck());
  if(status==EFI_NOT_READY)status=EFI_SUCCESS; /* inventory collected */
  goto finish;
#else
  /* GA107M experimental: HP firmware boots the dGPU with PCI memory decode
   * disabled, so the pinned-hash PROM gate and the cold-state pins are only
   * verifiable after decode is on. Enable decode FIRST — the one
   * non-transaction config write, via PciIo Attributes, never
   * EFI_PCI_DEVICE_ENABLE (which would also set bus mastering) — then run
   * the full gate sequence against real hardware state. */
  Stage="decode-enable";status=Step(EnableDecode());if(EFI_ERROR(status))goto finish;
  Stage="prom";status=Step(PromCheck());if(EFI_ERROR(status))goto finish;
  Stage="cold-state";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
  /* Post-decode strict validation: with decode on, the structural parse and
     the pinned config dwords must now pass against real hardware state. */
  Stage="config-strict";status=Step(ConfigCheck(FALSE));if(EFI_ERROR(status))goto finish;
#endif
  Stage="ownership";status=GuClaimController();
  if(status==EFI_ACCESS_DENIED) {
    status=GuFindReleaseOwner(St,&PendingOwner,TRUE);
    if(!EFI_ERROR(status))GuLogText("Exclusive ownership deferred until manual U and explicit driver release.\n");
  }
  status=Step(status);if(EFI_ERROR(status))goto finish;
  Stage="config-recheck";status=Step(ConfigCheck(FALSE));if(EFI_ERROR(status))goto finish;
  Stage="bar0";status=Step(BarCheck());if(EFI_ERROR(status))goto finish;
  Stage="prom";status=Step(PromCheck());if(EFI_ERROR(status))goto finish;
  Stage="cold-state";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
  Stage="assets";status=Step(GuLoadAssets());if(EFI_ERROR(status))goto finish;
  Stage="arming";status=Step(Arm());if(EFI_ERROR(status))goto finish;
  Stage="armed-config";status=Step(ConfigCheck(FALSE));if(EFI_ERROR(status))goto finish;
  Stage="armed-display";Gu.display_ok=DisplayCheck();status=Step(Gu.display_ok?EFI_SUCCESS:EFI_UNSUPPORTED);if(EFI_ERROR(status))goto finish;
  Stage="armed-cold-state";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
  if(PendingOwner.agent) {
    GU_RELEASE_OWNER current;
    Stage="owner-recheck";
    GuLogText("Manual U confirmed: release RTX firmware driver once, then revalidate.\n");
    if(!GuLogReady()){status=EFI_DEVICE_ERROR;goto finish;}
    /* Durable intent precedes the last owner snapshot. No file IO between a
       successful final comparison and DisconnectController. */
    status=GuFindReleaseOwner(St,&current,FALSE);
    if(!EFI_ERROR(status) && !GuSameReleaseOwner(&PendingOwner,&current))status=EFI_ABORTED;
    if(EFI_ERROR(status)){status=Step(status);goto finish;}
    EFI_PCI_IO_PROTOCOL *prior=Gu.pci;
    Stage="driver-release";release_attempted=TRUE;Gu.armed=TRUE;Gu.pci=NULL;
    EFI_STATUS released=Gu.bs->DisconnectController(Gu.handle,current.agent,NULL);
    EFI_TPL refresh_tpl=Gu.bs->RaiseTPL(TPL_NOTIFY);
    EFI_STATUS refreshed=GuRefreshTargetPci();
    Gu.bs->RestoreTPL(refresh_tpl);
    GuLog("driver release: status=%#llx refresh=%#llx same_pci=%u\n",
      (unsigned long long)released,(unsigned long long)refreshed,(unsigned)(Gu.pci==prior));
    status=Step(EFI_ERROR(released)?released:EFI_ERROR(refreshed)?refreshed:Gu.pci!=prior?EFI_COMPROMISED_DATA:EFI_SUCCESS);
    if(EFI_ERROR(status))goto finish;
    Stage="ownership-after-release";status=Step(GuClaimController());if(EFI_ERROR(status))goto finish;
    Stage="display-after-release";Gu.display_ok=DisplayCheck();status=Step(Gu.display_ok?EFI_SUCCESS:EFI_UNSUPPORTED);if(EFI_ERROR(status))goto finish;
    Stage="decode-after-release";status=Step(RestoreDecode());if(EFI_ERROR(status))goto finish;
    Stage="config-after-release";status=Step(ConfigCheck(FALSE));if(EFI_ERROR(status))goto finish;
    Stage="bar0-after-release";status=Step(BarCheck());if(EFI_ERROR(status))goto finish;
    Stage="prom-after-release";status=Step(PromCheck());if(EFI_ERROR(status))goto finish;
    Stage="cold-after-release";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
  }
  Stage="final-prom";status=Step(PromCheck());if(EFI_ERROR(status))goto finish;
  Stage="final-cold";status=Step(ColdCheck());if(EFI_ERROR(status))goto finish;
  if(!Gu.profile_verified || Gu.fault){status=EFI_ACCESS_DENIED;goto finish;}
  GuLogText("ARMED: one transaction is starting. Do not reset or remove the USB.\n");
  if(!GuLogReady()){status=EFI_DEVICE_ERROR;goto finish;}
  Stage="transaction";Gu.armed=TRUE;attempted=TRUE;
  UINT64 mask=0xffffffffULL;
  struct pci_dev device={.dev={.dma_mask=&mask,.coherent_dma_mask=0xffffffffULL},.pm_cap=Gu.pm_cap,.refs=1};
  struct gpu_unlock_610_shared shared={0};
  int result=gpu_unlock_610_load_shared(&device,&shared);
  if(!result)result=gpu_unlock_610_unlock(&device,&shared);
  /* Also covers failures before GuStarted, including BME-enable readback. */
  EFI_STATUS contained=GuClearMaster();
  LogContainment();
  GuLog("transaction result=%d containment=%#llx started=%u quiesced=%u fault=%u retained=%u maps=%u FLR=%u\n",
    result,(unsigned long long)contained,Gu.started,Gu.quiesced,Gu.fault,Gu.retained,Gu.dma_count,Gu.flrs);
  if(EFI_ERROR(contained) || !GuSafeToReturn() || Gu.allocations || (Gu.started && result))Halt();
  gpu_unlock_610_release_shared(&shared);Gu.armed=FALSE;
  status=result?EFI_DEVICE_ERROR:EFI_SUCCESS;
 finish:
  if(release_attempted && (EFI_ERROR(status) || Gu.fault))ReleaseHalt(status,attempted);
  if(attempted && !GuSafeToReturn())Halt();
  Gu.armed=FALSE;
  if(GuLogReady())GuLog("FINAL status=%#llx stage=%s attempted=%u driver_release=%u clean=%u\n",(unsigned long long)status,Stage,attempted,release_attempted,GuSafeToReturn());
  else GuConsole("Preflight or logging failed; no new attempt will start.\n");
  if(!GuLogReady()){status=EFI_DEVICE_ERROR;if(release_attempted)ReleaseHalt(status,attempted);if(Gu.started)Halt();}
  if(attempted && !GuSafeToReturn())Halt();
  EFI_STATUS closed=GuIoClose();
  if(EFI_ERROR(closed)){if(release_attempted)ReleaseHalt(closed,attempted);if(Gu.started)Halt();status=closed;}
  if(Gu.owned) {
    closed=Gu.bs->CloseProtocol(Gu.handle,&PciGuid,Image,NULL);
    if(EFI_ERROR(closed)){if(release_attempted)ReleaseHalt(closed,attempted);if(Gu.started)Halt();status=closed;}else Gu.owned=FALSE;
  }
  GuReleaseAssets();
#if GU_PREFLIGHT_ONLY
  if(!EFI_ERROR(status))GuConsole("Preflight inventory complete. Returning to firmware; transcribe REPORT.TXT pins.\n");
  else GuConsole("Preflight failed; no GPU state was modified. Returning to firmware.\n");
#elif GU_PROBE_MODE
  if(!EFI_ERROR(status))GuConsole("Probe complete: decode enabled, cold registers and PROM recorded.\nNo MMIO writes, DMA, firmware execution or selector writes occurred. Returning to firmware.\n");
  else GuConsole("Probe failed or was rejected. The only write attempted was the MEM decode bit.\nReturning to firmware.\n");
#else
  if(!EFI_ERROR(status))GuConsole("UEFI transaction completed and cleaned up. Returning to firmware; Windows validation is pending.\n");
  else GuConsole("Attempt cancelled or rejected. Returning without another attempt.\n");
#endif
  return status;
}
