#include "stm32f103xe.h"
#include <stdint.h>

uint32_t SystemCoreClock = 8000000U;

void SystemInit(void) {
  SCB->VTOR = FLASH_BASE;
  __DSB();
  __ISB();
}

void SystemCoreClockUpdate(void) { SystemCoreClock = 8000000U; }

/* The Cube startup calls this hook; the bootloader has no C constructors. */
void __libc_init_array(void) {}
