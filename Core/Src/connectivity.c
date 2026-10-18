/*
 * connectivity.c
 *
 *  Created on: Aug 27, 2024
 *      Author: thuanphat7
 */
#include "cJSON.h"
#include "main.h"
#include "ota_update.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <math.h>
//#include "config.h"
extern UART_HandleTypeDef huart1;
char array_json[200];
// float Percentage_battery;

#define MODEM_EVENT_BUFFER_SIZE 1536U
#define MQTT_RX_FRAME_TIMEOUT_MS 2000U

static const char mqtt_rx_start_marker[] = "+CMQTTRXSTART:";

static uint8_t modem_event_buffer[MODEM_EVENT_BUFFER_SIZE];
static uint8_t modem_process_buffer[MODEM_EVENT_BUFFER_SIZE];
static volatile uint16_t modem_event_length;
static char mqtt_rx_window[MODEM_EVENT_BUFFER_SIZE];
static uint16_t mqtt_rx_window_length;
static bool mqtt_rx_frame_in_progress;
static uint32_t mqtt_rx_frame_started_at;

static bool get_rpc_request_id(const char *message, char *request_id,
                               uint16_t request_id_size) {
  const char *request_topic =
      strstr(message, "v1/devices/me/rpc/request/");
  uint16_t index = 0;

  if (request_topic == NULL || request_id_size == 0) {
    return false;
  }

  request_topic += strlen("v1/devices/me/rpc/request/");
  while (request_topic[index] >= '0' && request_topic[index] <= '9' &&
         index < request_id_size - 1) {
    request_id[index] = request_topic[index];
    index++;
  }
  request_id[index] = '\0';

  return index > 0;
}

static void queue_rpc_response(const char *request_id, cJSON *response) {
  char *response_string = cJSON_PrintUnformatted(response);
  if (response_string != NULL) {
    snprintf(rpc_response_request_id, sizeof(rpc_response_request_id), "%s",
             request_id);
    snprintf(rpc_response_payload, sizeof(rpc_response_payload), "%s",
             response_string);
    is_rpc_response_pending = true;
    current_status_simcom = Response;
    free(response_string);
  }
}

static void handle_mqtt_payload(const char *topic, uint16_t topic_length,
                                const char *payload, uint16_t payload_length) {
  char topic_text[128];
  char json_payload[200];

  if (topic_length == 0U || topic_length >= sizeof(topic_text) ||
      payload_length == 0U || payload_length >= sizeof(json_payload)) {
    printf("[MQTT] rejected frame topic_len=%u payload_len=%u\r\n",
           (unsigned int)topic_length, (unsigned int)payload_length);
    return;
  }

  memcpy(topic_text, topic, topic_length);
  topic_text[topic_length] = '\0';
  memcpy(json_payload, payload, payload_length);
  json_payload[payload_length] = '\0';

  cJSON *json = cJSON_Parse(json_payload);
  if (json == NULL) {
    printf("[MQTT] cannot parse JSON on topic %s\r\n", topic_text);
    return;
  }

  if (strcmp(topic_text, MQTT_TOPIC_OTA_ATTRIBUTES) == 0) {
    cJSON *ota = cJSON_GetObjectItemCaseSensitive(json, "OTA");
    if (cJSON_IsString(ota) && ota->valuestring != NULL &&
        strcmp(ota->valuestring, "ota_check") == 0) {
      ota_request_start();
      printf("[OTA] MQTT request accepted and queued (modem_state=%d)\r\n",
             (int)current_status_simcom);
    } else {
      printf("[OTA] attributes payload ignored: %s\r\n", json_payload);
    }
    cJSON_Delete(json);
    return;
  }

  if (strstr(topic_text, "v1/devices/me/rpc/request/") == NULL) {
    printf("[MQTT] ignored topic: %s\r\n", topic_text);
    cJSON_Delete(json);
    return;
  }

  cJSON *method = cJSON_GetObjectItemCaseSensitive(json, "method");
  cJSON *params = cJSON_GetObjectItemCaseSensitive(json, "params");
  if (!cJSON_IsString(method) || method->valuestring == NULL) {
    cJSON_Delete(json);
    return;
  }

  if (strcmp(method->valuestring, "setRelays") != 0) {
    cJSON_Delete(json);
    return;
  }

  if (cJSON_IsObject(params)) {
    char request_id[16];
    int applied_count = 0;
    cJSON *response = cJSON_CreateObject();
    cJSON *applied = cJSON_CreateObject();

    if (response == NULL || applied == NULL) {
      cJSON_Delete(response);
      cJSON_Delete(applied);
      cJSON_Delete(json);
      return;
    }

    cJSON *relay_param = NULL;
    cJSON_ArrayForEach(relay_param, params) {
      if (relay_param->string == NULL) {
        continue;
      }

      int relay_number = atoi(relay_param->string);
      int relay_status = -1;

      if (relay_number < 1 || relay_number > NUMBER_LOADS) {
        continue;
      }

      if (cJSON_IsNumber(relay_param)) {
        relay_status = relay_param->valueint;
      } else if (cJSON_IsBool(relay_param)) {
        relay_status = cJSON_IsTrue(relay_param) ? 1 : 0;
      }

      if (relay_status == 1) {
        printf("-----------ON RELAY %d -----------\r\n", relay_number);
        HAL_GPIO_WritePin(GPIO_LOAD_PORT[relay_number - 1],
                          GPIO_LOAD_PIN[relay_number - 1], 1);
        cJSON_AddNumberToObject(applied, relay_param->string, relay_status);
        applied_count++;
        check_mqtt_signal_after_restart = true;
      } else if (relay_status == 0) {
        printf("-----------OFF RELAY %d -----------\r\n", relay_number);
        HAL_GPIO_WritePin(GPIO_LOAD_PORT[relay_number - 1],
                          GPIO_LOAD_PIN[relay_number - 1], 0);
        cJSON_AddNumberToObject(applied, relay_param->string, relay_status);
        applied_count++;
        check_mqtt_signal_after_restart = true;
      }
    }

    if (applied_count > 0 &&
        get_rpc_request_id(topic_text, request_id, sizeof(request_id)) == true) {
      cJSON_AddBoolToObject(response, "success", true);
      cJSON_AddItemToObject(response, "applied", applied);
      applied = NULL;

      queue_rpc_response(request_id, response);
    }

    cJSON_Delete(response);
    cJSON_Delete(applied);
  }

  cJSON_Delete(json);
}

void send_to_simcom_a76xx(char *cmd) {
  printf("STM32 Write: %s", cmd);
  HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000);
}

static void restart_modem_receive(void) {
  HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_IT(
      &huart1, (uint8_t *)rx_buffer, RX_BUFFER_UART1);
  if (status == HAL_OK) {
    return;
  }
  ota_uart_rx_error(0x80000000UL | (uint32_t)status);
  HAL_UART_AbortReceive(&huart1);
  __HAL_UART_CLEAR_OREFLAG(&huart1);
  status = HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)rx_buffer,
                                      RX_BUFFER_UART1);
  if (status != HAL_OK) {
    ota_uart_rx_error(0x40000000UL | (uint32_t)status);
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart->Instance == USART1) {
    uint16_t text_size = Size;
    uint16_t queued = modem_event_length;

    if (ota_uart_rx_capture((const uint8_t *)rx_buffer, Size)) {
      restart_modem_receive();
      return;
    }
    if (text_size >= RX_BUFFER_UART1) {
      text_size = RX_BUFFER_UART1 - 1U;
    }
    memcpy(rx_data_sim, rx_buffer, text_size);
    rx_data_sim[text_size] = '\0';

    if ((uint32_t)queued + text_size >= MODEM_EVENT_BUFFER_SIZE) {
      queued = 0U;
    }
    memcpy(&modem_event_buffer[queued], rx_buffer, text_size);
    modem_event_length = (uint16_t)(queued + text_size);

    memset(rx_buffer, 0, RX_BUFFER_UART1);
    restart_modem_receive();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    ota_uart_rx_error(HAL_UART_GetError(huart));
    HAL_UART_AbortReceive(huart);
    __HAL_UART_CLEAR_OREFLAG(huart);
    restart_modem_receive();
  }
}

static void append_mqtt_window(const uint8_t *data, uint16_t length) {
  uint16_t capacity = (uint16_t)(sizeof(mqtt_rx_window) - 1U);
  if (length >= capacity) {
    data += length - capacity;
    length = capacity;
    mqtt_rx_window_length = 0U;
  } else if ((uint32_t)mqtt_rx_window_length + length > capacity) {
    uint16_t remove =
        (uint16_t)(mqtt_rx_window_length + length - capacity);
    memmove(mqtt_rx_window, mqtt_rx_window + remove,
            mqtt_rx_window_length - remove);
    mqtt_rx_window_length = (uint16_t)(mqtt_rx_window_length - remove);
  }
  memcpy(mqtt_rx_window + mqtt_rx_window_length, data, length);
  mqtt_rx_window_length = (uint16_t)(mqtt_rx_window_length + length);
  mqtt_rx_window[mqtt_rx_window_length] = '\0';
}

static void mqtt_frame_mark_pending(void) {
  if (!mqtt_rx_frame_in_progress) {
    mqtt_rx_frame_in_progress = true;
    mqtt_rx_frame_started_at = HAL_GetTick();
  }
}

static void mqtt_frame_clear(void) {
  mqtt_rx_frame_in_progress = false;
  mqtt_rx_frame_started_at = 0U;
}

static uint16_t mqtt_start_marker_suffix_length(void) {
  uint16_t marker_length = (uint16_t)(sizeof(mqtt_rx_start_marker) - 1U);
  uint16_t candidate = mqtt_rx_window_length;

  if (candidate >= marker_length) {
    candidate = (uint16_t)(marker_length - 1U);
  }
  while (candidate > 0U) {
    if (memcmp(&mqtt_rx_window[mqtt_rx_window_length - candidate],
               mqtt_rx_start_marker, candidate) == 0) {
      return candidate;
    }
    candidate--;
  }
  return 0U;
}

bool simcom_mqtt_rx_pending(void) {
  if (!mqtt_rx_frame_in_progress) {
    return false;
  }
  if ((HAL_GetTick() - mqtt_rx_frame_started_at) < MQTT_RX_FRAME_TIMEOUT_MS) {
    return true;
  }

  printf("[MQTT] receive frame timeout; dropping %u buffered bytes\r\n",
         (unsigned int)mqtt_rx_window_length);
  mqtt_rx_window_length = 0U;
  mqtt_rx_window[0] = '\0';
  mqtt_frame_clear();
  return false;
}

static char *mqtt_data_after_header(char *header) {
  char *line_end = strstr(header, "\r\n");
  if (line_end != NULL) {
    return line_end + 2;
  }
  line_end = strchr(header, '\n');
  return line_end != NULL ? line_end + 1 : NULL;
}

static void process_mqtt_window(void) {
  while (mqtt_rx_window_length > 0U) {
    char *start = strstr(mqtt_rx_window, mqtt_rx_start_marker);
    char *end;
    char *line_end;
    char *topic_header;
    char *topic_data;
    char *payload_header;
    char *payload_data;
    unsigned int topic_length;
    unsigned int payload_length;
    uint16_t consumed;

    if (start == NULL) {
      uint16_t suffix_length = mqtt_start_marker_suffix_length();
      if (suffix_length > 0U) {
        memmove(mqtt_rx_window,
                &mqtt_rx_window[mqtt_rx_window_length - suffix_length],
                suffix_length);
        mqtt_rx_window_length = suffix_length;
        mqtt_rx_window[suffix_length] = '\0';
        mqtt_frame_mark_pending();
      } else {
        mqtt_rx_window_length = 0U;
        mqtt_rx_window[0] = '\0';
        mqtt_frame_clear();
      }
      return;
    }
    if (start != mqtt_rx_window) {
      uint16_t keep = (uint16_t)(mqtt_rx_window_length -
                                 (uint16_t)(start - mqtt_rx_window));
      memmove(mqtt_rx_window, start, keep);
      mqtt_rx_window_length = keep;
      mqtt_rx_window[keep] = '\0';
    }
    mqtt_frame_mark_pending();
    end = strstr(mqtt_rx_window, "+CMQTTRXEND:");
    if (end == NULL) {
      return;
    }

    line_end = strstr(end, "\r\n");
    if (line_end != NULL) {
      consumed = (uint16_t)((line_end + 2) - mqtt_rx_window);
    } else {
      line_end = strchr(end, '\n');
      if (line_end != NULL) {
        consumed = (uint16_t)((line_end + 1) - mqtt_rx_window);
      } else {
        char *end_value = end + strlen("+CMQTTRXEND:");
        while (*end_value == ' ') {
          end_value++;
        }
        if (*end_value < '0' || *end_value > '9') {
          return;
        }
        while (*end_value >= '0' && *end_value <= '9') {
          end_value++;
        }
        if (*end_value == '\r' &&
            end_value + 1 == mqtt_rx_window + mqtt_rx_window_length) {
          end_value++;
        }
        if (end_value != mqtt_rx_window + mqtt_rx_window_length) {
          return;
        }
        consumed = mqtt_rx_window_length;
      }
    }

    mqtt_rx_window[consumed] = '\0';

    topic_header = strstr(mqtt_rx_window, "+CMQTTRXTOPIC:");
    payload_header = strstr(mqtt_rx_window, "+CMQTTRXPAYLOAD:");
    if (topic_header == NULL || payload_header == NULL ||
        topic_header >= payload_header || payload_header >= end ||
        sscanf(topic_header, "+CMQTTRXTOPIC: %*u,%u", &topic_length) != 1 ||
        sscanf(payload_header, "+CMQTTRXPAYLOAD: %*u,%u", &payload_length) !=
            1) {
      printf("[MQTT] malformed SIMCom receive frame\r\n");
    } else {
      topic_data = mqtt_data_after_header(topic_header);
      payload_data = mqtt_data_after_header(payload_header);

      if (topic_data == NULL || payload_data == NULL ||
          topic_data > payload_header || payload_data > end ||
          topic_length > UINT16_MAX || payload_length > UINT16_MAX ||
          topic_length > (unsigned int)(payload_header - topic_data) ||
          payload_length > (unsigned int)(end - payload_data)) {
        printf("[MQTT] incomplete SIMCom receive frame\r\n");
      } else {
        handle_mqtt_payload(topic_data, (uint16_t)topic_length, payload_data,
                            (uint16_t)payload_length);
      }
    }

    memmove(mqtt_rx_window, mqtt_rx_window + consumed,
            mqtt_rx_window_length - consumed);
    mqtt_rx_window_length = (uint16_t)(mqtt_rx_window_length - consumed);
    mqtt_rx_window[mqtt_rx_window_length] = '\0';
    mqtt_frame_clear();
  }
}

void process_simcom_rx(void) {
  uint16_t length;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  length = modem_event_length;
  if (length > 0U) {
    memcpy(modem_process_buffer, modem_event_buffer, length);
    modem_event_length = 0U;
  }
  if (primask == 0U) {
    __enable_irq();
  }
  if (length == 0U) {
    return;
  }
  modem_process_buffer[length] = '\0';
  printf("\r\nSIMCOM Response:%.*s", length,
         (const char *)modem_process_buffer);

  append_mqtt_window(modem_process_buffer, length);
  process_mqtt_window();
  if (strstr((const char *)modem_process_buffer, "+CMQTTCONNLOST") != NULL &&
      is_pb_done == true) {
    printf("-----------------Client disconnected passively------------------\n");
    current_status_simcom = On;
  }
}

void create_JSON(void) {
  cJSON *json = cJSON_CreateObject();
  rssi = read_signal_quality();
  for (int i = 1; i < NUMBER_LOADS + 1; i++) {
    int status_load;
    status_load = HAL_GPIO_ReadPin(GPIO_LOAD_PORT[i - 1], GPIO_LOAD_PIN[i - 1]);
    static char payload1[3];
    sprintf(payload1, "%d", i);
    cJSON_AddNumberToObject(json, payload1, status_load);
  }
  cJSON_AddNumberToObject(json, "_gsm_signal_strength", rssi);
  cJSON_AddStringToObject(json, "_version", VERSION_MANTIS);
  char *json_string = cJSON_PrintUnformatted(json);
  if (json_string == NULL) {
    printf("New create error JSON\n");
    cJSON_Delete(json);
    return;
  }
  sprintf(array_json, "%s", json_string);
  // decompress memory
  free(json_string);
  cJSON_Delete(json);
}
