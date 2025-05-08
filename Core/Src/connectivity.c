/*
 * connectivity.c
 *
 *  Created on: Aug 27, 2024
 *      Author: thuanphat7
 */
#include "cJSON.h"
#include "config.h"
#include "main.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <math.h>
extern UART_HandleTypeDef huart1;
char array_json[200];
int pin_1x;
// float Percentage_battery;

void send_to_simcom_a76xx(char *cmd) {
  printf("STM32 Write: %s", cmd);
  HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart->Instance == USART1) {
    printf("\r\nSIMCOM Response:");
    printf(rx_buffer);
//    static int times;
//    times = strlen(rx_buffer);
    for (int i = 0; i < 1500; i++) {
      rx_data_sim[i] = rx_buffer[i];
      if ((char)rx_buffer[i] == (char)SERIAL_NUMBER[5] &&
          (char)rx_buffer[i + 1] == (char)SERIAL_NUMBER[6] &&
          (char)rx_buffer[i + 2] == (char)SERIAL_NUMBER[7] &&
          current_status_simcom == Subscribed) {
        load_port = (rx_buffer[i + 4] - 48);
        pin_1x = (rx_buffer[(i + 5)] - 48);
        if (pin_1x == 0) {
          load_port = 10;
        }
#if SIMCOM_MODEL == a7672s || SIMCOM_MODEL == a7680 || SIMCOM_MODEL == a7670sa
        if ((rx_buffer[(i + 29)] == 49 && is_pb_done == true)||(load_port ==10&& rx_buffer[(i + 30)] == 49))
#elif SIMCOM_MODEL == a7670c
        if ((rx_buffer[(i + 31)] == 49 && is_pb_done == true)||(load_port ==10&& rx_buffer[(i + 32)] == 49))
#elif SIMCOM_MODEL == a7080
        if (rx_buffer[(i + 8)] == 49 && is_pb_done == true)
#endif
        {
          printf("-----------ON RELAY %d -----------\r\n", load_port);
          HAL_GPIO_WritePin(GPIO_LOAD_PORT[load_port - 1],
                            GPIO_LOAD_PIN[load_port - 1], 1);
          check_mqtt_signal_after_restart = true;
        }
#if SIMCOM_MODEL == a7672s || SIMCOM_MODEL == a7680 || SIMCOM_MODEL == a7670sa
        if ((rx_buffer[(i + 29)] == 48 && is_pb_done == true)||(load_port ==10&& rx_buffer[(i + 30)] == 48))
#elif SIMCOM_MODEL == a7670c
        if ((rx_buffer[(i + 31)] == 48 && is_pb_done == true)||(load_port ==10&& rx_buffer[(i + 32)] == 48))
#elif SIMCOM_MODEL == a7080
        if (rx_buffer[(i + 8)] == 48 && is_pb_done == true)
#endif
        {
          printf("-----------OFF RELAY %d -----------\r\n", load_port);
          HAL_GPIO_WritePin(GPIO_LOAD_PORT[load_port - 1],
                            GPIO_LOAD_PIN[load_port - 1], 0);
        }
      }
    }
    if ((strstr((char *)rx_buffer, "+CMQTTCONNLOST") != NULL) &&
        is_pb_done == true) {
      printf(
          "-----------------Client Disconnect passively!------------------\n");
      current_status_simcom = On;
    }
  }
  memset(rx_buffer, '\0', 1500);
  HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)rx_buffer, 1500);
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
