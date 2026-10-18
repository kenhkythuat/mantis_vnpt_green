/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cJSON.h"
#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include <math.h>
#include "config.h"
#include "ota_update.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
int _write(int file, char *ptr, int len) {

  int i;
  for (i = 0; i < len; i++) {
    ITM_SendChar(*ptr++);
  }
  return len;
}

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
IWDG_HandleTypeDef hiwdg;

TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
GPIO_TypeDef *GPIO_LOAD_PORT[10] = {PAYLOAD_1_GPIO_Port, PAYLOAD_2_GPIO_Port,
                                    PAYLOAD_3_GPIO_Port, PAYLOAD_4_GPIO_Port,
                                    PAYLOAD_5_GPIO_Port, PAYLOAD_6_GPIO_Port,
                                    PAYLOAD_7_GPIO_Port, PAYLOAD_8_GPIO_Port,
                                    PAYLOAD_9_GPIO_Port, PAYLOAD_10_GPIO_Port};
unsigned int GPIO_LOAD_PIN[10] = {
    PAYLOAD_1_Pin, PAYLOAD_2_Pin, PAYLOAD_3_Pin, PAYLOAD_4_Pin, PAYLOAD_5_Pin,
    PAYLOAD_6_Pin, PAYLOAD_7_Pin, PAYLOAD_8_Pin, PAYLOAD_9_Pin, PAYLOAD_10_Pin};
char array_at_command[400];
float signal_strength = 0;
int on_relay = 0;
int rssi = -99;
int is_pb_done = 0;
int load_port;

char rx_buffer[RX_BUFFER_UART1];
char rx_data_sim[RX_BUFFER_UART1];
int previousTick;
int is_connect_simcom = 0;
int is_connect_mqtt = 0;
float data_percentage_pin;
int to_send_status_to_server;
uint16_t frequency_1hz = 0;

bool is_enable_mqtt = false;
bool is_connected_mqtt = false;
bool is_checked_sim = false;
bool is_subcribed_mqtt = false;
bool is_published_mqtt = false;
bool is_acquiered_mqtt = false;
bool is_updated_status = false;
bool is_rpc_response_pending = false;
uint8_t total_errors = 0;

enum GmsModemState current_status_simcom = Off;
uint8_t check_error_internet;
uint8_t check_error_mqtt;

bool check_mqtt_signal_after_restart = false;
uint16_t time_after_restart;
char rpc_response_request_id[16];
char rpc_response_payload[200];
static bool ota_request_pending;
static bool ota_tim6_was_running;
static volatile bool relay_shutdown_pending;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM6_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_IWDG_Init(void);
/* USER CODE BEGIN PFP */
void enable_simcom(void); // reset module simcom A76XX
void send_to_simcom_a76xx(char *cmd);
void led_status(char cmd);
static void pause_non_ota_io(void);
static void resume_non_ota_io(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == htim6.Instance) {
    if (current_status_simcom >= Subscribed) {
      frequency_1hz++;
      if (frequency_1hz >= INTERVAL_PUPLISH_DATA) {
        to_send_status_to_server = 1;
        frequency_1hz = 0;
      }
    }
    if (!check_mqtt_signal_after_restart) {
      time_after_restart++;
      if (time_after_restart > CHECK_TIME_AFTER_MQTT_RESTART) {
        relay_shutdown_pending = true;
        check_mqtt_signal_after_restart = true;
      }
    }
  }
}
void enable_simcom(void) {
  led_status('R');
  printf("Enable SIMCOM\n");
#if SIMCOM_MODEL == a7080
  send_to_simcom_a76xx("AT+CPOWD=1\r\n");
  HAL_Delay(5000);
  HAL_GPIO_WritePin(A76XX_PWRKEY_GPIO_Port, A76XX_PWRKEY_Pin, SET);
  HAL_Delay(4000);
  HAL_GPIO_WritePin(A76XX_PWRKEY_GPIO_Port, A76XX_PWRKEY_Pin, RESET);
  HAL_Delay(4000);
#else
  HAL_GPIO_WritePin(A76XX_PWRKEY_GPIO_Port, A76XX_PWRKEY_Pin, SET);
  HAL_Delay(3300);
  HAL_GPIO_WritePin(A76XX_PWRKEY_GPIO_Port, A76XX_PWRKEY_Pin, RESET);
  HAL_Delay(3300);
  HAL_GPIO_WritePin(A76XX_PWRKEY_GPIO_Port, A76XX_PWRKEY_Pin, SET);
  HAL_Delay(500);
  HAL_GPIO_WritePin(A76XX_PWRKEY_GPIO_Port, A76XX_PWRKEY_Pin, RESET);
  HAL_Delay(1000);
#endif
}
void led_status(char cmd) {
  if (cmd == 'R') {
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, SET);
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, SET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, RESET);
  } else if (cmd == 'B') {
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, SET);
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, SET);
  } else if (cmd == 'G') {
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, RESET);
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, SET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, SET);
  } else if (cmd == 'W') {
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, RESET);
    HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, RESET);
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, RESET);
  }
}

static void pause_non_ota_io(void) {
  ota_tim6_was_running = (htim6.Instance->CR1 & TIM_CR1_CEN) != 0U;
  to_send_status_to_server = 0;
  HAL_TIM_Base_Stop_IT(&htim6);
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
  HAL_NVIC_ClearPendingIRQ(TIM6_IRQn);
  /* USART1 is the only active data path during OTA and must preempt all
   * remaining interrupts while receiving binary HTTP chunks. */
  HAL_NVIC_SetPriority(USART1_IRQn, 0U, 0U);
  printf("[OTA] non-OTA I/O paused; USART1 priority=0\r\n");
}

static void resume_non_ota_io(void) {
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
  if (ota_tim6_was_running) {
    HAL_TIM_Base_Start_IT(&htim6);
  }
}
void check_handle_state(enum GmsModemState status) {
  switch (status) {
  case Off: {
    enable_simcom();
    IWDG->KR = 0xAAAA;
    is_pb_done = wait_for_pb_done_event();
    if (is_pb_done) {
      current_status_simcom = On;
      printf("Current status Simcom On \r\n");
    } else {
      NVIC_SystemReset();
    }
    break;
  }
  case On: {
    IWDG->KR = 0xAAAA;
    is_checked_sim = check_signal_simcom();
    if (is_checked_sim) {
      total_errors = 0;
      current_status_simcom = InternetReady;
      printf("Simcom current status is Internet ready\r\n");
    } else {
      total_errors++;
    }
    if (total_errors > 10) {
      restart_stm32();
    }
    break;
  }
  case InternetReady: {
    IWDG->KR = 0xAAAA;
    is_enable_mqtt = enable_mqtt_on_gsm_modem();
#if SIMCOM_MODEL != a7080
    if (is_enable_mqtt) {
      is_acquiered_mqtt = acquire_gsm_mqtt_client();
    } else {
      NVIC_SystemReset();
    }
    if (is_acquiered_mqtt) {
      is_connected_mqtt = connect_mqtt_server_by_gsm();
    }
#endif
    if (is_connected_mqtt) {
      total_errors = 0;
      led_status('G');
      current_status_simcom = MqttReady;
      printf("Simcom current status is MQTT ready\r\n");
    } else
      total_errors++;
    if (total_errors > 3) {
      restart_stm32();
    }
    break;
  }
  case MqttReady: {
    IWDG->KR = 0xAAAA;
    is_subcribed_mqtt = subscribe_mqtt_via_gsm();
    if (is_subcribed_mqtt) {
      total_errors = 0;
      current_status_simcom = Subscribed;
      printf("\r\nCurrent status Simcom Subscribed \r\n");
    } else
      total_errors++;
    if (total_errors > 3) {
      restart_stm32();
    }
    break;
  }
  case Subscribed: {
    if (to_send_status_to_server) {
      is_updated_status = send_payload_signal_to_server();
      if (is_updated_status) {
        to_send_status_to_server = 0;
        is_updated_status=0;
        IWDG->KR = 0xAAAA;
        total_errors = 0;
      } else {
        total_errors++;
        if (total_errors > 3) {
          stop_mqtt_via_gsm();
          current_status_simcom = On;
        }
      }
    }
    break;
  }
  case Response: {
    printf("STM32 response data server !\r\n");
    if (is_rpc_response_pending) {
      IWDG->KR = 0xAAAA;
      if (publish_rpc_response_via_gsm()) {
        is_rpc_response_pending = false;
        rpc_response_request_id[0] = '\0';
        rpc_response_payload[0] = '\0';
        current_status_simcom = Subscribed;
        total_errors = 0;
      } else {
        total_errors++;
        if (total_errors > 3) {
          is_rpc_response_pending = false;
          rpc_response_request_id[0] = '\0';
          rpc_response_payload[0] = '\0';
          stop_mqtt_via_gsm();
          current_status_simcom = On;
        }
      }
    } else {
      current_status_simcom = Subscribed;
    }
    break;
  }
  default:
    printf("Case cannot be determined !\r\n");
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM6_Init();
  MX_USART1_UART_Init();
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */
  HAL_Delay(500U);
  printf("-----Welcome to Agriconnect-----\n");
  printf("-----Hello Mantis-----\n");
#if SAVE_LOAD
  control_vavle_form_flash();
#endif
  HAL_UARTEx_ReceiveToIdle_IT(&huart1, (uint8_t *)rx_buffer, RX_BUFFER_UART1);
  ota_init();
  HAL_TIM_Base_Start_IT(&htim6);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    process_simcom_rx();
    if (relay_shutdown_pending) {
      relay_shutdown_pending = false;
      for (int i = 0; i < NUMBER_LOADS; i++) {
        HAL_GPIO_WritePin(GPIO_LOAD_PORT[i], GPIO_LOAD_PIN[i], GPIO_PIN_RESET);
      }
      write_status_load();
    }
    if (ota_take_request()) {
      ota_request_pending = true;
    }
    if (ota_request_pending && current_status_simcom == Subscribed) {
      ota_result_t ota_result;
      ota_request_pending = false;
      printf("[OTA] starting update check\r\n");
      pause_non_ota_io();
      ota_result = ota_check_and_download();
      if (ota_result == OTA_RESULT_OK) {
        printf("[OTA] image verified; resetting to bootloader\r\n");
        HAL_Delay(500U);
        NVIC_SystemReset();
        while (1) {
          IWDG->KR = 0xAAAAU;
        }
      } else if (ota_result == OTA_RESULT_NO_UPDATE) {
        resume_non_ota_io();
        printf("[OTA] firmware is already current\r\n");
      } else if (ota_result == OTA_RESULT_BUSY) {
        resume_non_ota_io();
        printf("[OTA] update is already running\r\n");
      } else {
        resume_non_ota_io();
        printf("[OTA] update failed\r\n");
      }
    } else {
      check_handle_state(current_status_simcom);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Reload = 4095;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 35999;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 1999;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED_BLUE_Pin|LED_GREEN_Pin|LED_RED_Pin|A76XX_PWRKEY_Pin
                          |PAYLOAD_1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED_STATUS_Pin|PAYLOAD_6_Pin|PAYLOAD_7_Pin|PAYLOAD_8_Pin
                          |PAYLOAD_9_Pin|PAYLOAD_10_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, PAYLOAD_2_Pin|PAYLOAD_3_Pin|PAYLOAD_4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(PAYLOAD_5_GPIO_Port, PAYLOAD_5_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LED_BLUE_Pin LED_GREEN_Pin LED_RED_Pin A76XX_PWRKEY_Pin
                           PAYLOAD_1_Pin */
  GPIO_InitStruct.Pin = LED_BLUE_Pin|LED_GREEN_Pin|LED_RED_Pin|A76XX_PWRKEY_Pin
                          |PAYLOAD_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_STATUS_Pin PAYLOAD_6_Pin PAYLOAD_7_Pin PAYLOAD_8_Pin
                           PAYLOAD_9_Pin PAYLOAD_10_Pin */
  GPIO_InitStruct.Pin = LED_STATUS_Pin|PAYLOAD_6_Pin|PAYLOAD_7_Pin|PAYLOAD_8_Pin
                          |PAYLOAD_9_Pin|PAYLOAD_10_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PAYLOAD_2_Pin PAYLOAD_3_Pin PAYLOAD_4_Pin */
  GPIO_InitStruct.Pin = PAYLOAD_2_Pin|PAYLOAD_3_Pin|PAYLOAD_4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PAYLOAD_5_Pin */
  GPIO_InitStruct.Pin = PAYLOAD_5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PAYLOAD_5_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
