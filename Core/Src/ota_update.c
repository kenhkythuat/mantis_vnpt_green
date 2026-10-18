#include "ota_update.h"

#include "cJSON.h"
#include "config.h"
#include "main.h"
#include "ota_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OTA_UART_CAPTURE_SIZE 768U
#define OTA_HTTP_READ_CHUNK   256U
#define OTA_HTTP_READ_RETRIES 3U
#define OTA_HTTP_RX_QUIET_MS  20U
#define OTA_HTTP_BODY_TIMEOUT_MS    30000U
#define OTA_HTTP_TRAILER_TIMEOUT_MS 1000U
#define OTA_MANIFEST_MAX_SIZE 600U

extern UART_HandleTypeDef huart1;

static volatile bool s_ota_requested;

void ota_request_start(void) { s_ota_requested = true; }

bool ota_take_request(void) {
  bool requested;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  requested = s_ota_requested;
  s_ota_requested = false;
  if (primask == 0U) {
    __enable_irq();
  }
  return requested;
}

#if OTA_ENABLE

static uint8_t s_uart_capture[OTA_UART_CAPTURE_SIZE];
static volatile uint16_t s_uart_capture_len;
static volatile bool s_uart_capture_active;
static volatile uint32_t s_uart_error_count;
static volatile uint32_t s_uart_last_error;
static bool s_ota_busy;
static char s_manifest_json[OTA_MANIFEST_MAX_SIZE];

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len) {
  crc = ~crc;
  for (uint32_t i = 0U; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; bit++) {
      uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

static void watchdog_feed(void) { IWDG->KR = 0xAAAAU; }

static void capture_begin(void) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  s_uart_capture_active = false;
  s_uart_capture_len = 0U;
  memset(s_uart_capture, 0, sizeof(s_uart_capture));
  s_uart_capture_active = true;
  if (primask == 0U) {
    __enable_irq();
  }
}

static void capture_end(void) {
  s_uart_capture_active = false;
  __DMB();
}

bool ota_uart_rx_capture(const uint8_t *data, uint16_t length) {
  uint16_t write_at;
  uint16_t available;

  if (!s_uart_capture_active || data == NULL || length == 0U) {
    return false;
  }
  write_at = s_uart_capture_len;
  if (write_at >= (OTA_UART_CAPTURE_SIZE - 1U)) {
    return true;
  }
  available = (uint16_t)(OTA_UART_CAPTURE_SIZE - 1U - write_at);
  if (length > available) {
    length = available;
  }
  memcpy(&s_uart_capture[write_at], data, length);
  write_at = (uint16_t)(write_at + length);
  s_uart_capture[write_at] = '\0';
  __DMB();
  s_uart_capture_len = write_at;
  return true;
}

void ota_uart_rx_error(uint32_t error_code) {
  s_uart_last_error = error_code;
  s_uart_error_count++;
}

static int32_t capture_find(const char *needle) {
  uint16_t used = s_uart_capture_len;
  size_t needle_len = strlen(needle);

  if (needle_len == 0U || used < needle_len) {
    return -1;
  }
  for (uint16_t i = 0U; i <= (uint16_t)(used - needle_len); i++) {
    if (memcmp(&s_uart_capture[i], needle, needle_len) == 0) {
      return (int32_t)i;
    }
  }
  return -1;
}

static bool capture_has_modem_error(void) {
  uint16_t used = s_uart_capture_len;

  /* Compare bytes directly so the OTA image does not embed the complete
   * modem error marker that legacy firmware mistakes for an HTTP body error. */
  if (used < 9U) {
    return false;
  }
  for (uint16_t i = 0U; i <= (uint16_t)(used - 9U); i++) {
    if (s_uart_capture[i] == '\r' && s_uart_capture[i + 1U] == '\n' &&
        s_uart_capture[i + 2U] == 'E' && s_uart_capture[i + 3U] == 'R' &&
        s_uart_capture[i + 4U] == 'R' && s_uart_capture[i + 5U] == 'O' &&
        s_uart_capture[i + 6U] == 'R' && s_uart_capture[i + 7U] == '\r' &&
        s_uart_capture[i + 8U] == '\n') {
      return true;
    }
  }
  return false;
}

static bool wait_for_text(const char *text, uint32_t timeout_ms) {
  uint32_t started = HAL_GetTick();
  while ((HAL_GetTick() - started) < timeout_ms) {
    if (capture_find(text) >= 0) {
      return true;
    }
    if (capture_has_modem_error()) {
      return false;
    }
    watchdog_feed();
    HAL_Delay(10U);
  }
  return false;
}

static bool send_command_wait(const char *command, const char *expected,
                              uint32_t timeout_ms) {
  bool ok;
  capture_begin();
  send_to_simcom_a76xx((char *)command);
  ok = wait_for_text(expected, timeout_ms);
  if (!ok) {
    uint16_t used = s_uart_capture_len;
    printf("[OTA] command failed: %s", command);
    printf("[OTA] response (%u bytes): %.*s\r\n", used, used,
           (const char *)s_uart_capture);
  }
  capture_end();
  return ok;
}

static void http_terminate_if_active(void) {
  uint32_t started;
  capture_begin();
  send_to_simcom_a76xx("AT+HTTPTERM\r\n");
  started = HAL_GetTick();
  while ((HAL_GetTick() - started) < 1500U) {
    if (capture_find("\r\nOK\r\n") >= 0 ||
        capture_has_modem_error()) {
      break;
    }
    watchdog_feed();
    HAL_Delay(10U);
  }
  capture_end();
}

static bool flash_erase_pages(uint32_t start_addr, uint32_t size) {
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0U;

  if (size == 0U || (start_addr % OTA_FLASH_PAGE_SIZE) != 0U) {
    return false;
  }
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.PageAddress = start_addr;
  erase.NbPages = (size + OTA_FLASH_PAGE_SIZE - 1U) / OTA_FLASH_PAGE_SIZE;
  HAL_FLASH_Unlock();
  if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
    HAL_FLASH_Lock();
    printf("[OTA] erase failed at 0x%08lX\r\n", (unsigned long)page_error);
    return false;
  }
  HAL_FLASH_Lock();
  return true;
}

static bool flash_program_bytes(uint32_t address, const uint8_t *data,
                                uint32_t length) {
  if ((address & 1U) != 0U || data == NULL) {
    return false;
  }
  HAL_FLASH_Unlock();
  for (uint32_t i = 0U; i < length; i += 2U) {
    uint16_t value = data[i];
    if ((i + 1U) < length) {
      value |= (uint16_t)data[i + 1U] << 8U;
    } else {
      value |= 0xFF00U;
    }
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + i, value) !=
        HAL_OK) {
      HAL_FLASH_Lock();
      printf("[OTA] write failed at 0x%08lX\r\n",
             (unsigned long)(address + i));
      return false;
    }
  }
  HAL_FLASH_Lock();
  return true;
}

static bool metadata_valid(const ota_metadata_t *metadata) {
  uint32_t header_crc;
  if (metadata->magic != OTA_METADATA_MAGIC ||
      metadata->metadata_version != OTA_METADATA_VERSION ||
      metadata->state != OTA_STATE_PENDING ||
      metadata->app_addr != OTA_APP_START_ADDR ||
      metadata->staging_addr != OTA_STAGING_START_ADDR ||
      metadata->image_size < 8U || metadata->image_size > OTA_APP_MAX_SIZE ||
      metadata->image_size > OTA_STAGING_MAX_SIZE) {
    return false;
  }
  header_crc = crc32_update(0U, (const uint8_t *)metadata,
                            sizeof(*metadata) - sizeof(metadata->header_crc32));
  return header_crc == metadata->header_crc32;
}

bool ota_metadata_is_pending(void) {
  return metadata_valid((const ota_metadata_t *)OTA_METADATA_ADDR);
}

static bool save_pending_metadata(const ota_manifest_t *manifest) {
  ota_metadata_t metadata;
  memset(&metadata, 0, sizeof(metadata));
  metadata.magic = OTA_METADATA_MAGIC;
  metadata.metadata_version = OTA_METADATA_VERSION;
  metadata.state = OTA_STATE_PENDING;
  metadata.app_addr = OTA_APP_START_ADDR;
  metadata.staging_addr = OTA_STAGING_START_ADDR;
  metadata.image_size = manifest->size;
  metadata.image_crc32 = manifest->crc32;
  snprintf(metadata.firmware_version, sizeof(metadata.firmware_version), "%s",
           manifest->version);
  metadata.header_crc32 =
      crc32_update(0U, (const uint8_t *)&metadata,
                   sizeof(metadata) - sizeof(metadata.header_crc32));

  if (!flash_erase_pages(OTA_METADATA_ADDR, OTA_FLASH_PAGE_SIZE)) {
    return false;
  }
  return flash_program_bytes(OTA_METADATA_ADDR, (const uint8_t *)&metadata,
                             sizeof(metadata));
}

static int version_component(const char **cursor) {
  int value = 0;
  while (**cursor >= '0' && **cursor <= '9') {
    value = value * 10 + (**cursor - '0');
    (*cursor)++;
  }
  if (**cursor == '.') {
    (*cursor)++;
  }
  return value;
}

static int version_compare(const char *left, const char *right) {
  for (uint8_t part = 0U; part < 4U; part++) {
    int left_value = version_component(&left);
    int right_value = version_component(&right);
    if (left_value != right_value) {
      return left_value > right_value ? 1 : -1;
    }
  }
  return 0;
}

static uint32_t json_u32(cJSON *json, const char *key) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  if (item == NULL) {
    return 0U;
  }
  if (cJSON_IsString(item) && item->valuestring != NULL) {
    return (uint32_t)strtoul(item->valuestring, NULL, 0);
  }
  if (cJSON_IsNumber(item)) {
    return (uint32_t)item->valuedouble;
  }
  return 0U;
}

static bool http_open(const char *url, uint32_t *content_length) {
  int method = -1;
  int status = 0;
  int length = 0;
  int32_t action_at;

  http_terminate_if_active();
  if (!send_command_wait("AT+CSSLCFG=\"sslversion\",0,4\r\n", "OK", 2000U) ||
      !send_command_wait("AT+CSSLCFG=\"authmode\",0,0\r\n", "OK", 2000U) ||
      !send_command_wait("AT+CSSLCFG=\"enableSNI\",0,1\r\n", "OK", 2000U) ||
      !send_command_wait("AT+HTTPINIT\r\n", "OK", 3000U) ||
      !send_command_wait("AT+HTTPPARA=\"SSLCFG\",0\r\n", "OK", 2000U) ||
      !send_command_wait("AT+HTTPPARA=\"CONNECTTO\",120\r\n", "OK", 2000U) ||
      !send_command_wait("AT+HTTPPARA=\"RECVTO\",120\r\n", "OK", 2000U) ||
      !send_command_wait("AT+HTTPPARA=\"READMODE\",1\r\n", "OK", 2000U)) {
    return false;
  }
  if (snprintf(array_at_command, sizeof(array_at_command),
               "AT+HTTPPARA=\"URL\",\"%s\"\r\n", url) >=
      (int)sizeof(array_at_command) ||
      !send_command_wait(array_at_command, "OK", 3000U)) {
    return false;
  }

  capture_begin();
  send_to_simcom_a76xx("AT+HTTPACTION=0\r\n");
  if (!wait_for_text("+HTTPACTION:", 120000U)) {
    capture_end();
    printf("[OTA] HTTPACTION timeout\r\n");
    return false;
  }
  action_at = capture_find("+HTTPACTION:");
  if (action_at < 0 ||
      sscanf((char *)&s_uart_capture[action_at], "+HTTPACTION: %d,%d,%d",
             &method, &status, &length) != 3) {
    capture_end();
    printf("[OTA] HTTPACTION parse failed\r\n");
    return false;
  }
  capture_end();
  printf("[OTA] HTTP status=%d length=%d\r\n", status, length);
  if (method != 0 || status < 200 || status >= 300 || length <= 0) {
    return false;
  }
  *content_length = (uint32_t)length;
  return true;
}

static void http_close(void) { http_terminate_if_active(); }

static bool parse_httpread_header(uint16_t *body_at, uint16_t *body_length) {
  static const char header[] = "+HTTPREAD:";
  uint16_t used = s_uart_capture_len;
  const uint16_t header_len = (uint16_t)(sizeof(header) - 1U);

  for (uint16_t start = 0U; start + header_len <= used; start++) {
    uint16_t cursor;
    uint32_t value = 0U;
    bool has_digit = false;
    if (memcmp(&s_uart_capture[start], header, header_len) != 0) {
      continue;
    }
    cursor = (uint16_t)(start + header_len);
    while (cursor < used &&
           (s_uart_capture[cursor] == ' ' || s_uart_capture[cursor] == 'D' ||
            s_uart_capture[cursor] == 'A' || s_uart_capture[cursor] == 'T' ||
            s_uart_capture[cursor] == ',')) {
      cursor++;
    }
    while (cursor < used && s_uart_capture[cursor] >= '0' &&
           s_uart_capture[cursor] <= '9') {
      has_digit = true;
      value = value * 10U + (uint32_t)(s_uart_capture[cursor] - '0');
      cursor++;
    }
    if (!has_digit || value == 0U || value > OTA_HTTP_READ_CHUNK) {
      continue;
    }
    while ((cursor + 1U) < used) {
      if (s_uart_capture[cursor] == '\r' &&
          s_uart_capture[cursor + 1U] == '\n') {
        *body_at = (uint16_t)(cursor + 2U);
        *body_length = (uint16_t)value;
        return true;
      }
      cursor++;
    }
  }
  return false;
}

static bool http_read_binary(uint32_t offset, uint16_t requested,
                             uint8_t *output, uint16_t *received) {
  uint32_t read_errors_before = s_uart_error_count;

  for (uint8_t attempt = 1U; attempt <= OTA_HTTP_READ_RETRIES; attempt++) {
    uint32_t started;
    uint16_t body_at = 0U;
    uint16_t body_length = 0U;
    uint16_t capture_length = 0U;
    bool header_found = false;
    bool body_complete = false;
    bool uart_error = false;
    uint32_t rx_changed_at;
    uint32_t body_complete_at = 0U;
    uint32_t errors_before = s_uart_error_count;

    *received = 0U;
    snprintf(array_at_command, sizeof(array_at_command),
             "AT+HTTPREAD=%lu,%u\r\n", (unsigned long)offset, requested);
    capture_begin();
    send_to_simcom_a76xx(array_at_command);
    started = HAL_GetTick();
    rx_changed_at = started;
    while (true) {
      uint32_t now = HAL_GetTick();
      uint16_t current_capture_length = s_uart_capture_len;
      if (current_capture_length != capture_length) {
        capture_length = current_capture_length;
        rx_changed_at = now;
      }
      if (s_uart_error_count != errors_before) {
        uart_error = true;
      }
      header_found = parse_httpread_header(&body_at, &body_length);
      if (header_found && body_length > 0U && body_length == requested &&
          body_at <= (uint16_t)(UINT16_MAX - body_length) &&
          capture_length >= (uint16_t)(body_at + body_length)) {
        if (!body_complete) {
          body_complete_at = now;
        }
        body_complete = true;
      }
      /* STM32F1 stalls instruction fetch briefly while programming flash.
       * Do not start a flash write until the complete HTTP response, including
       * a delayed trailer, has stopped arriving on USART1. */
      if (body_complete && !uart_error &&
          (now - rx_changed_at) >= OTA_HTTP_RX_QUIET_MS) {
        uint32_t elapsed = now - started;
        memcpy(output, &s_uart_capture[body_at], body_length);
        *received = body_length;
        capture_end();
        if (elapsed >= 1000U) {
          printf("[OTA] HTTPREAD slow offset=%lu completed in %lums\r\n",
                 (unsigned long)offset, (unsigned long)elapsed);
        }
        return true;
      }
      /* Firmware is arbitrary binary and may itself contain "\r\nERROR\r\n".
       * Once a valid +HTTPREAD data header exists, that byte sequence belongs
       * to the body and must not be interpreted as a modem response. */
      if (uart_error ||
          (!header_found && capture_has_modem_error())) {
        break;
      }
      /* Body timeout applies only until all requested bytes are present. A
       * body that finishes near that deadline still gets its own trailer/idle
       * window instead of being incorrectly rejected. */
      if ((!body_complete &&
           (now - started) >= OTA_HTTP_BODY_TIMEOUT_MS) ||
          (body_complete &&
           (now - body_complete_at) >= OTA_HTTP_TRAILER_TIMEOUT_MS)) {
        break;
      }
      watchdog_feed();
      HAL_Delay(1U);
    }
    capture_end();
    printf("[OTA] HTTPREAD %u/%u failed offset=%lu requested=%u "
           "captured=%u header=%u body=%u complete=%u errors=%lu "
           "elapsed=%lums\r\n",
           attempt, OTA_HTTP_READ_RETRIES, (unsigned long)offset, requested,
           s_uart_capture_len, header_found ? 1U : 0U, body_length,
           body_complete ? 1U : 0U,
           (unsigned long)(s_uart_error_count - errors_before),
           (unsigned long)(HAL_GetTick() - started));
    if (attempt < OTA_HTTP_READ_RETRIES) {
      HAL_Delay(250U);
    }
  }
  if (s_uart_error_count != read_errors_before) {
    printf("[OTA] HTTPREAD failed offset=%lu UART errors=%lu last=0x%08lX\r\n",
           (unsigned long)offset,
           (unsigned long)(s_uart_error_count - read_errors_before),
           (unsigned long)s_uart_last_error);
  } else {
    printf("[OTA] HTTPREAD failed offset=%lu without a new UART error\r\n",
           (unsigned long)offset);
  }
  return false;
}

static bool http_get_text(const char *url, char *output, uint32_t capacity) {
  uint32_t content_length;
  uint32_t offset = 0U;
  if (!http_open(url, &content_length)) {
    return false;
  }
  if (content_length == 0U || content_length >= capacity) {
    printf("[OTA] manifest length invalid=%lu\r\n",
           (unsigned long)content_length);
    http_close();
    return false;
  }
  while (offset < content_length) {
    uint32_t remaining = content_length - offset;
    uint16_t requested = remaining > OTA_HTTP_READ_CHUNK
                             ? OTA_HTTP_READ_CHUNK
                             : (uint16_t)remaining;
    uint16_t received = 0U;
    if (!http_read_binary(offset, requested, (uint8_t *)&output[offset],
                          &received) ||
        received != requested) {
      http_close();
      return false;
    }
    offset += received;
  }
  output[content_length] = '\0';
  http_close();
  return true;
}

static bool load_manifest(ota_manifest_t *manifest) {
  cJSON *json;
  cJSON *version;
  cJSON *bin_url;
  cJSON *device;

  memset(manifest, 0, sizeof(*manifest));
  if (!http_get_text(OTA_MANIFEST_URL, s_manifest_json,
                     sizeof(s_manifest_json))) {
    printf("[OTA] manifest download failed\r\n");
    return false;
  }
  json = cJSON_Parse(s_manifest_json);
  if (json == NULL) {
    printf("[OTA] manifest JSON invalid\r\n");
    return false;
  }
  version = cJSON_GetObjectItemCaseSensitive(json, "version");
  bin_url = cJSON_GetObjectItemCaseSensitive(json, "bin_url");
  device = cJSON_GetObjectItemCaseSensitive(json, "device");
  if (!cJSON_IsString(version) || !cJSON_IsString(bin_url) ||
      !cJSON_IsString(device) || version->valuestring == NULL ||
      bin_url->valuestring == NULL || device->valuestring == NULL ||
      strcmp(device->valuestring, OTA_DEVICE_ID) != 0 ||
      strlen(version->valuestring) >= sizeof(manifest->version) ||
      strlen(bin_url->valuestring) >= sizeof(manifest->bin_url)) {
    printf("[OTA] manifest fields/device invalid\r\n");
    cJSON_Delete(json);
    return false;
  }
  snprintf(manifest->version, sizeof(manifest->version), "%s",
           version->valuestring);
  snprintf(manifest->bin_url, sizeof(manifest->bin_url), "%s",
           bin_url->valuestring);
  manifest->app_addr = json_u32(json, "app_addr");
  manifest->size = json_u32(json, "size");
  manifest->crc32 = json_u32(json, "crc32");
  cJSON_Delete(json);

  if (manifest->app_addr != OTA_APP_START_ADDR || manifest->size < 8U ||
      manifest->size > OTA_APP_MAX_SIZE ||
      manifest->size > OTA_STAGING_MAX_SIZE || manifest->crc32 == 0U) {
    printf("[OTA] manifest address/size/CRC invalid\r\n");
    return false;
  }
  return true;
}

static bool download_firmware(const ota_manifest_t *manifest) {
  uint8_t chunk[OTA_HTTP_READ_CHUNK];
  uint32_t content_length;
  uint32_t offset = 0U;
  uint32_t crc = 0U;

  if (!http_open(manifest->bin_url, &content_length)) {
    return false;
  }
  if (content_length != manifest->size) {
    printf("[OTA] binary length mismatch HTTP=%lu manifest=%lu\r\n",
           (unsigned long)content_length, (unsigned long)manifest->size);
    http_close();
    return false;
  }
  if (!flash_erase_pages(OTA_STAGING_START_ADDR, manifest->size)) {
    http_close();
    return false;
  }
  while (offset < manifest->size) {
    uint32_t remaining = manifest->size - offset;
    uint16_t requested = remaining > sizeof(chunk) ? sizeof(chunk)
                                                   : (uint16_t)remaining;
    uint16_t received = 0U;
    if (!http_read_binary(offset, requested, chunk, &received) ||
        received != requested ||
        !flash_program_bytes(OTA_STAGING_START_ADDR + offset, chunk, received)) {
      http_close();
      return false;
    }
    crc = crc32_update(crc, chunk, received);
    offset += received;
    watchdog_feed();
    if ((offset % 4096U) == 0U || offset == manifest->size) {
      printf("[OTA] downloaded %lu/%lu\r\n", (unsigned long)offset,
             (unsigned long)manifest->size);
    }
  }
  http_close();
  if (crc != manifest->crc32) {
    printf("[OTA] stream CRC mismatch got=0x%08lX expected=0x%08lX\r\n",
           (unsigned long)crc, (unsigned long)manifest->crc32);
    return false;
  }
  crc = crc32_update(0U, (const uint8_t *)OTA_STAGING_START_ADDR,
                     manifest->size);
  if (crc != manifest->crc32) {
    printf("[OTA] flash CRC mismatch got=0x%08lX expected=0x%08lX\r\n",
           (unsigned long)crc, (unsigned long)manifest->crc32);
    return false;
  }
  return true;
}

void ota_init(void) {
  s_ota_requested = false;
  s_uart_capture_active = false;
  s_uart_capture_len = 0U;
  s_uart_error_count = 0U;
  s_uart_last_error = 0U;
  s_ota_busy = false;
  if (ota_metadata_is_pending()) {
    printf("[OTA] pending image found; reset into bootloader required\r\n");
  }
}

ota_result_t ota_check_and_download(void) {
  ota_manifest_t manifest;
  ota_result_t result = OTA_RESULT_ERROR;
  if (s_ota_busy) {
    return OTA_RESULT_BUSY;
  }
  s_ota_busy = true;
  printf("[OTA] checking manifest\r\n");
  if (!load_manifest(&manifest)) {
    goto done;
  }
  printf("[OTA] remote=%s current=%s\r\n", manifest.version,
         VERSION_MANTIS);
  if (version_compare(manifest.version, VERSION_MANTIS) <= 0) {
    result = OTA_RESULT_NO_UPDATE;
    goto done;
  }
  if (!download_firmware(&manifest)) {
    printf("[OTA] firmware download/verify failed\r\n");
    goto done;
  }
  if (!save_pending_metadata(&manifest)) {
    printf("[OTA] metadata commit failed\r\n");
    goto done;
  }
  printf("[OTA] verified image committed for bootloader\r\n");
  result = OTA_RESULT_OK;

done:
  capture_end();
  s_ota_busy = false;
  return result;
}

#else

void ota_init(void) { s_ota_requested = false; }
ota_result_t ota_check_and_download(void) { return OTA_RESULT_NO_UPDATE; }
bool ota_metadata_is_pending(void) { return false; }
bool ota_uart_rx_capture(const uint8_t *data, uint16_t length) {
  (void)data;
  (void)length;
  return false;
}
void ota_uart_rx_error(uint32_t error_code) { (void)error_code; }

#endif
