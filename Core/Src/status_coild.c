/*
 * status_coild.c
 *
 *  Created on: Aug 31, 2024
 *      Author: admin
 */

#include "config.h"
#include "main.h"
#include "stdio.h"

#define FLASH_START_ADDR 0x0800F000

int read_status;
uint8_t status_loads;
void reset_flash(uint32_t numberpages) {
  HAL_FLASH_Unlock();
  FLASH_EraseInitTypeDef pEraseInit;
  pEraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
  pEraseInit.PageAddress = FLASH_START_ADDR;
  pEraseInit.NbPages = numberpages;
  uint32_t PageError = 0;
  HAL_FLASHEx_Erase(&pEraseInit, &PageError);
  HAL_FLASH_Lock();
}

void control_vavle_form_flash(void) {
  uint8_t binary_value;
  binary_value = *(uint8_t *)(FLASH_START_ADDR);
  for (int i = 0; i < NUMBER_LOADS; i++) {
    uint8_t temp;
    temp = ((binary_value >> i) & 1);
    HAL_GPIO_WritePin(GPIO_LOAD_PORT[i], GPIO_LOAD_PIN[i], temp);
  }
}

void write_flash(int move, uint32_t Data) {
  HAL_FLASH_Unlock();
  HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_START_ADDR + move, Data);
  HAL_FLASH_Lock();
}

void write_status_load() {
  reset_flash(1);
  status_loads = 0;
  for (int i = 0; i < NUMBER_LOADS; i++) {
    uint8_t read_status_load;
    read_status_load = HAL_GPIO_ReadPin(GPIO_LOAD_PORT[i], GPIO_LOAD_PIN[i]);
    status_loads |= (read_status_load << i);
  }
  write_flash(0, status_loads);
}
