#ifndef INC_DEVICE_CONFIG_H_
#define INC_DEVICE_CONFIG_H_

#include "ota_layout.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_CONFIG_MAGIC          0x4D434647UL /* MCFG */
#define DEVICE_CONFIG_SCHEMA_VERSION 1U
#define DEVICE_SERIAL_NUMBER_SIZE    32U
#define DEVICE_MQTT_CLIENT_ID_SIZE   64U
#define DEVICE_MQTT_USER_SIZE        64U
#define DEVICE_MQTT_PASS_SIZE        64U

typedef struct {
  uint32_t magic;
  uint16_t schema_version;
  uint16_t struct_size;
  char serial_number[DEVICE_SERIAL_NUMBER_SIZE];
  char mqtt_client_id[DEVICE_MQTT_CLIENT_ID_SIZE];
  char mqtt_user[DEVICE_MQTT_USER_SIZE];
  char mqtt_pass[DEVICE_MQTT_PASS_SIZE];
  uint32_t crc32;
} device_config_t;

typedef char device_config_must_fit_one_flash_page[
    (sizeof(device_config_t) <= OTA_FLASH_PAGE_SIZE) ? 1 : -1];
typedef char device_config_must_have_even_size[
    ((sizeof(device_config_t) & 1U) == 0U) ? 1 : -1];

static inline uint32_t device_config_calculate_crc(const device_config_t *config) {
  const uint8_t *data = (const uint8_t *)config;
  uint32_t crc = ~0UL;
  for (uint32_t i = 0U; i < (uint32_t)offsetof(device_config_t, crc32); i++) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; bit++) {
      uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

static inline bool device_config_string_valid(const char *value,
                                               uint32_t capacity) {
  if (value[0] == '\0') {
    return false;
  }
  for (uint32_t i = 1U; i < capacity; i++) {
    if (value[i] == '\0') {
      return true;
    }
  }
  return false;
}

static inline bool device_config_is_valid(const device_config_t *config) {
  return config->magic == DEVICE_CONFIG_MAGIC &&
         config->schema_version == DEVICE_CONFIG_SCHEMA_VERSION &&
         config->struct_size == sizeof(device_config_t) &&
         device_config_string_valid(config->serial_number,
                                    DEVICE_SERIAL_NUMBER_SIZE) &&
         device_config_string_valid(config->mqtt_client_id,
                                    DEVICE_MQTT_CLIENT_ID_SIZE) &&
         device_config_string_valid(config->mqtt_user, DEVICE_MQTT_USER_SIZE) &&
         device_config_string_valid(config->mqtt_pass, DEVICE_MQTT_PASS_SIZE) &&
         device_config_calculate_crc(config) == config->crc32;
}

static inline const device_config_t *device_config_get(void) {
  const device_config_t *config =
      (const device_config_t *)OTA_FACTORY_CONFIG_ADDR;
  return device_config_is_valid(config) ? config : NULL;
}

#endif /* INC_DEVICE_CONFIG_H_ */
