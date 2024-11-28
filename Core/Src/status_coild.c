/*
 * status_coild.c
 *
 *  Created on: Aug 31, 2024
 *      Author: admin
 */

#include "config.h"
#include "main.h"
#include "stdio.h"

#define Address 0x08000000 + 1024 * 60

int val;
int read_status;
uint32_t status;
uint32_t value_page0;
uint32_t value_page1;
uint32_t value_page2;
uint32_t value_page3;
uint32_t value_Relay;
uint32_t status_load[NUMBER_LOADS];
void reset_flash(uint32_t numberpages)
{
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef pEraseInit;
    pEraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    pEraseInit.PageAddress = Address;
    pEraseInit.NbPages = numberpages;
    uint32_t PageError = 0;
    HAL_FLASHEx_Erase(&pEraseInit, &PageError);
    HAL_FLASH_Lock();
}

void read_flash_payload(void)
{
    for (int i = 0; i <= NUMBER_LOADS; i++) {
        static int temp;
        temp = read_page(Address + (i * 16));
        HAL_GPIO_WritePin(GPIO_LOAD_PORT[load_port + i], GPIO_LOAD_PIN[load_port + i], temp);
    }
}

uint32_t read_page(uint32_t Address_ex)
{
    value_page0 = *(uint32_t *)(Address_ex);
    return value_page0;
}
void write_flash(int move, uint32_t Data)
{
    HAL_FLASH_Unlock();
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, Address + move, Data);
    HAL_FLASH_Lock();
}

void write_load_statues()
{
    reset_flash(1);
    for (int i = 0; i < NUMBER_LOADS; i++) {
        read_status = HAL_GPIO_ReadPin(GPIO_LOAD_PORT[i], GPIO_LOAD_PIN[i]);
        status_load[val] = read_status;
        val++;
        write_flash((i * 16), status_load[i]);
        HAL_Delay(100);
    }
    val = 0;
}
