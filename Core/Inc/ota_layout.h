#ifndef INC_OTA_LAYOUT_H_
#define INC_OTA_LAYOUT_H_

#include <stdint.h>

/* STM32F103RET6: 512 KiB flash, 2 KiB erase pages, 64 KiB SRAM. */
#define OTA_FLASH_BASE            0x08000000UL
#define OTA_FLASH_SIZE            (512UL * 1024UL)
#define OTA_FLASH_END_ADDR        (OTA_FLASH_BASE + OTA_FLASH_SIZE)
#define OTA_FLASH_PAGE_SIZE       0x00000800UL

#define OTA_BOOTLOADER_SIZE       0x00008000UL
#define OTA_BOOTLOADER_CODE_SIZE  0x00007800UL
#define OTA_FACTORY_CONFIG_ADDR   0x08007800UL
#define OTA_APP_START_ADDR        0x08008000UL
#define OTA_APP_MAX_SIZE          0x0003B800UL
#define OTA_STAGING_START_ADDR    0x08043800UL
#define OTA_STAGING_MAX_SIZE      0x0003B800UL
#define OTA_METADATA_ADDR         0x0807F000UL
#define OTA_RELAY_CONFIG_ADDR     0x0807F800UL
#define OTA_CONFIG_ADDR           OTA_RELAY_CONFIG_ADDR

#define OTA_SRAM_START_ADDR       0x20000000UL
#define OTA_SRAM_END_ADDR         0x20010000UL

#define OTA_METADATA_MAGIC        0x4D4E5431UL /* MNT1 */
#define OTA_METADATA_VERSION      0x00010000UL
#define OTA_STATE_PENDING         1UL

#if ((OTA_BOOTLOADER_SIZE % OTA_FLASH_PAGE_SIZE) != 0UL) || \
    ((OTA_BOOTLOADER_CODE_SIZE % OTA_FLASH_PAGE_SIZE) != 0UL) || \
    ((OTA_FACTORY_CONFIG_ADDR % OTA_FLASH_PAGE_SIZE) != 0UL) || \
    ((OTA_APP_START_ADDR % OTA_FLASH_PAGE_SIZE) != 0UL) || \
    ((OTA_APP_MAX_SIZE % OTA_FLASH_PAGE_SIZE) != 0UL) || \
    ((OTA_STAGING_START_ADDR % OTA_FLASH_PAGE_SIZE) != 0UL) || \
    ((OTA_STAGING_MAX_SIZE % OTA_FLASH_PAGE_SIZE) != 0UL) || \
    ((OTA_METADATA_ADDR % OTA_FLASH_PAGE_SIZE) != 0UL) || \
    ((OTA_CONFIG_ADDR % OTA_FLASH_PAGE_SIZE) != 0UL)
#error "OTA regions must be aligned to STM32F103RE flash pages"
#endif

#if ((OTA_FLASH_BASE + OTA_BOOTLOADER_CODE_SIZE) != OTA_FACTORY_CONFIG_ADDR) || \
    ((OTA_FACTORY_CONFIG_ADDR + OTA_FLASH_PAGE_SIZE) != OTA_APP_START_ADDR) || \
    (OTA_APP_START_ADDR != (OTA_FLASH_BASE + OTA_BOOTLOADER_SIZE)) || \
    ((OTA_APP_START_ADDR + OTA_APP_MAX_SIZE) != OTA_STAGING_START_ADDR) || \
    ((OTA_STAGING_START_ADDR + OTA_STAGING_MAX_SIZE) != OTA_METADATA_ADDR) || \
    ((OTA_METADATA_ADDR + OTA_FLASH_PAGE_SIZE) != OTA_CONFIG_ADDR) || \
    ((OTA_CONFIG_ADDR + OTA_FLASH_PAGE_SIZE) != OTA_FLASH_END_ADDR)
#error "OTA flash regions overlap or leave an unexpected gap"
#endif

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t metadata_version;
  uint32_t state;
  uint32_t app_addr;
  uint32_t staging_addr;
  uint32_t image_size;
  uint32_t image_crc32;
  char firmware_version[16];
  uint32_t header_crc32;
} ota_metadata_t;

#endif /* INC_OTA_LAYOUT_H_ */
