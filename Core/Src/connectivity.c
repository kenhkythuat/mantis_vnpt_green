/*
 * connectivity.c
 *
 *  Created on: Aug 27, 2024
 *      Author: thuanphat7
 */
#include "cJSON.h"
#include "main.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <math.h>
//#include "config.h"
extern UART_HandleTypeDef huart1;
char array_json[200];
// float Percentage_battery;

static bool get_rpc_request_id(char *request_id, uint16_t request_id_size) {
  char *request_topic = strstr(rx_buffer, "v1/devices/me/rpc/request/");
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

static void handle_relay_rpc_payload(void) {
  char *json_start = strchr(rx_buffer, '{');
  char *json_end = strrchr(rx_buffer, '}');

  if (json_start == NULL || json_end == NULL || json_end < json_start ||
      current_status_simcom != Subscribed || is_pb_done != true) {
    return;
  }

  char json_payload[200];
  uint16_t json_length = (uint16_t)(json_end - json_start + 1);

  if (json_length >= sizeof(json_payload)) {
    printf("RPC JSON payload is too long\r\n");
    return;
  }

  memcpy(json_payload, json_start, json_length);
  json_payload[json_length] = '\0';

  cJSON *json = cJSON_Parse(json_payload);
  if (json == NULL) {
    printf("Cannot parse RPC JSON payload\r\n");
    return;
  }

  cJSON *method = cJSON_GetObjectItemCaseSensitive(json, "method");
  cJSON *params = cJSON_GetObjectItemCaseSensitive(json, "params");
  if (!cJSON_IsString(method) || method->valuestring == NULL ||
      strcmp(method->valuestring, "setRelays") != 0) {
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
        get_rpc_request_id(request_id, sizeof(request_id)) == true) {
      cJSON_AddBoolToObject(response, "success", true);
      cJSON_AddItemToObject(response, "applied", applied);
      applied = NULL;

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

    cJSON_Delete(response);
    cJSON_Delete(applied);
  }

  cJSON_Delete(json);
}

void send_to_simcom_a76xx(char *cmd) {
  printf("STM32 Write: %s", cmd);
  HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart->Instance == USART1) {
    printf("\r\nSIMCOM Response:");
    printf("%s", rx_buffer);
    for (int i = 0; i < RX_BUFFER_UART1; i++) {
      rx_data_sim[i] = rx_buffer[i];
    }
    handle_relay_rpc_payload();
    if ((strstr((char *)rx_buffer, "+CMQTTCONNLOST") != NULL) &&
        is_pb_done == true) {
      printf(
          "-----------------Client Disconnect passively!------------------\n");
      current_status_simcom = On;
    }
  }
  memset(rx_buffer, '\0', RX_BUFFER_UART1);
  HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)rx_buffer, RX_BUFFER_UART1);
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
