/* GA107 0.1.1 laptop-FWSEC fix regression tests: DMEM transfer geometry.
 * Ported from ga107-unlock tests/test_vbios_610.c live-measured scenarios.
 * Hosted build only: compiled WITHOUT -D__KERNEL__ so GU_NOTE uses stdio. */
#include <stdio.h>
#include <errno.h>
#include "../vendor/vbios_610.h"

static int failures;

static void check(int cond, const char *what) {
  if (!cond) { printf("FAIL: %s\n", what); failures++; }
}

int main(void) {
  /* Live GA107 log: 0xe300 IMEM/data offset + 0x980 DMEM in a padded 0xed00 image. */
  u32 bytes = 0;
  struct gpu_unlock_fwsec geometry = {
    .image_size=0xed00, .data_off=0xe300, .dmem_size=0x980,
  };
  check(!gpu_unlock_fwsec_dmem_transfer(&geometry,&bytes) && bytes==0xa00,
    "0x980-byte DMEM rounds up to a 0xa00 transfer");
  geometry.image_size=0xec80;
  check(gpu_unlock_fwsec_dmem_transfer(&geometry,&bytes)==-ERANGE,
    "missing padding rejected (ERANGE)");
  geometry.image_size=0xed00; geometry.data_off++;
  check(gpu_unlock_fwsec_dmem_transfer(&geometry,&bytes)==-EINVAL,
    "unaligned data offset rejected (EINVAL)");
  geometry.data_off=0xe300; geometry.dmem_size=0xffffffff;
  check(gpu_unlock_fwsec_dmem_transfer(&geometry,&bytes)==-EINVAL,
    "overflowing DMEM size rejected (EINVAL)");
  geometry.dmem_size=0x200;
  check(!gpu_unlock_fwsec_dmem_transfer(&geometry,&bytes) && bytes==0x200,
    "already-aligned size passes through unchanged");
  if (failures) { printf("%d failures\n", failures); return 1; }
  puts("PASS GA107 laptop FWSEC dmem transfer geometry (0x980->0xa00 round-up)");
  return 0;
}
