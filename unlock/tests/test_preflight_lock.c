#define main legacy_port_main
#include "test_port.c"
#undef main
int main(void) {
  VOID *cpu=NULL;UINT64 dma=0;UINT32 value=4;
  setup();Gu.armed=Gu.owned=Gu.display_ok=TRUE;
  assert(EFI_ERROR(GuDmaAllocate(4096,&cpu,&dma)) && !cpu && !dma && !fake.allocs);
  GuWrite32(0,1);assert(Gu.fault && !fake.writes);
  setup();assert(EFI_ERROR(GuConfigWrite(EfiPciIoWidthUint32,4,&value)) && !fake.pciwrites);
  setup();assert(EFI_ERROR(GuEnableMaster()) && !fake.pciwrites);
  /* Containment (GuClearMaster) clears a stray BME bit with a config write;
   * it is deliberately allowed in every build. Assert it succeeds and the
   * bit ends clear rather than expecting a lock. */
  setup();config[4]|=4;assert(!EFI_ERROR(GuClearMaster()) && !(config[4]&4));
  setup();Gu.saved=TRUE;assert(EFI_ERROR(GuFlr()) && !fake.flr_seen && !fake.pciwrites);
  puts("PASS production preflight write/DMA/master locks and FLR gate with armed flag forced");return 0;
}
