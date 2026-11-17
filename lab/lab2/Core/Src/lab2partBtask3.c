/* Ron Ladin 213465974
   Dan Chetirkin 323126854
 */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MB_ADDRESS   1U   /* the Modbus address of the board */
#define MESSAGE_MAX 64U   /* bytes of one message, at most */
#define NUM_REGS     5U   /* holding registers 0 to 4 */
#define REG_LD2      0U   /* LD2: 1 on, 0 off */
#define REG_GOOD     1U   /* messages with a correct CRC */
#define REG_BAD      2U   /* messages with a wrong CRC */
#define REG_CRC_SW   3U   /* cycles of the CRC in software */
#define REG_CRC_HW   4U   /* cycles of the CRC in the CRC unit */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;

/* USER CODE BEGIN PV */
static uint8_t rx[MESSAGE_MAX];           /* the bytes that arrive */
static uint8_t message[MESSAGE_MAX];      /* a complete message */
static volatile uint16_t message_len = 0; /* its bytes; 0: none */
static uint16_t regs[NUM_REGS];           /* the holding registers */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* CRC-16/MODBUS of byte_count bytes. Polynomial 0xA001 (0x8005 with
   bits in reverse order), start value 0xFFFF. A message holds it low
   byte first. */
static uint16_t modbus_crc16(const uint8_t *data, uint32_t byte_count)
{
  uint16_t crc = 0xFFFFU;
  for (uint32_t i = 0; i < byte_count; i++) {
    crc ^= data[i];
    for (uint32_t b = 0; b < 8U; b++) {
      if ((crc & 1U) != 0U) {
        crc = (uint16_t)((crc >> 1) ^ 0xA001U);
      } else {
        crc = (uint16_t)(crc >> 1);
      }
    }
  }
  return crc;
}

/* A message ended (the line went quiet). Runs inside the interrupt. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  if (huart != &hlpuart1) return;
  if (message_len == 0U && size <= MESSAGE_MAX) {
    memcpy(message, rx, size);             /* yes: copy the message in */
    message_len = size;                    /* tell the loop "you have mail" */
  }                                        /* no: throw the new one away */
  HAL_UARTEx_ReceiveToIdle_IT(&hlpuart1, rx, MESSAGE_MAX); /* listen again */
}

/* Something went wrong (like overrun). The HAL stopped, so start again. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart == &hlpuart1) {
    HAL_UARTEx_ReceiveToIdle_IT(&hlpuart1, rx, MESSAGE_MAX);
  }
}

/* Configure CRC-16/MODBUS with reflected input bytes and result. */
static void crc_unit_init(void)
{
  __HAL_RCC_CRC_CLK_ENABLE();
  CRC->POL = 0x8005U;
  CRC->INIT = 0xFFFFU;
  CRC->CR = CRC_CR_POLYSIZE_0 | CRC_CR_REV_IN_0 | CRC_CR_REV_OUT;
}

static uint16_t crc_unit(const uint8_t *data, uint32_t byte_count)
{
  CRC->CR |= CRC_CR_RESET;
  for (uint32_t i = 0; i < byte_count; i++) {
    *(volatile uint8_t *)&CRC->DR = data[i];
  }
  return (uint16_t)CRC->DR;
}

/* Time both calculations and accept only if both match the received CRC. */
static int crc_ok(const uint8_t *message_bytes, uint16_t message_length)
{
  uint32_t byte_count = message_length - 2U;
  uint16_t received = (uint16_t)(message_bytes[byte_count]
                    | (message_bytes[byte_count + 1U] << 8));

  uint32_t start = DWT->CYCCNT;
  uint16_t crc_sw = modbus_crc16(message_bytes, byte_count);
  regs[REG_CRC_SW] = (uint16_t)(DWT->CYCCNT - start);

  start = DWT->CYCCNT;
  uint16_t crc_hw = crc_unit(message_bytes, byte_count);
  regs[REG_CRC_HW] = (uint16_t)(DWT->CYCCNT - start);

  return received == crc_sw && received == crc_hw;
}

/* Each response buffer reserves two bytes for the low-first CRC. */
static void reply(uint8_t *reply_bytes, uint16_t length_without_crc)
{
  uint16_t crc = modbus_crc16(reply_bytes, length_without_crc);
  reply_bytes[length_without_crc] = (uint8_t)crc;
  reply_bytes[length_without_crc + 1U] = (uint8_t)(crc >> 8);
  HAL_UART_Transmit(&hlpuart1, reply_bytes, length_without_crc + 2U, 100U);
}

static void exception(uint8_t function, uint8_t code)
{
  uint8_t response[5] = { MB_ADDRESS, (uint8_t)(function | 0x80U), code };
  reply(response, 3U);
}

static void handle_message(const uint8_t *message_bytes, uint16_t message_length)
{
  const uint8_t *m = message_bytes;
  if (message_length < 4U) return;                   /* too short: ignore */

  if (!crc_ok(m, message_length)) {                  /* broken by noise */
    regs[REG_BAD]++;
    return;                                          /* say nothing */
  }
  if (m[0] != MB_ADDRESS) return;                    /* not for me */
  regs[REG_GOOD]++;

  uint8_t function = m[1];
  if (function != 0x03U && function != 0x06U) {
    exception(function, 1U);
    return;
  }
  if (message_length != 8U) {
    exception(function, 3U);
    return;
  }

  uint16_t reg   = (uint16_t)((m[2] << 8) | m[3]);   /* high byte first */
  uint16_t value = (uint16_t)((m[4] << 8) | m[5]);

  if (function == 0x06U) {
    if (reg != REG_LD2) {
      exception(function, 2U);
      return;
    }
    regs[REG_LD2] = (value != 0U) ? 1U : 0U;
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, regs[REG_LD2] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_UART_Transmit(&hlpuart1, m, message_length, 100U);
    return;
  }

  uint16_t count = value;
  if (count == 0U || count > 125U) {
    exception(function, 3U);
    return;
  }
  if (reg >= NUM_REGS || count > NUM_REGS - reg) {
    exception(function, 2U);
    return;
  }

  uint8_t response[3U + 2U * NUM_REGS + 2U];
  response[0] = MB_ADDRESS;
  response[1] = function;
  response[2] = (uint8_t)(2U * count);
  for (uint16_t i = 0; i < count; i++) {
    response[3U + 2U * i] = (uint8_t)(regs[reg + i] >> 8);
    response[4U + 2U * i] = (uint8_t)regs[reg + i];
  }
  reply(response, (uint16_t)(3U + 2U * count));
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
  MX_LPUART1_UART_Init();
  /* USER CODE BEGIN 2 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* start the cycle counter */
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  HAL_NVIC_SetPriority(LPUART1_IRQn, 1, 0);        /* SysTick (0) may interrupt us */
  crc_unit_init();
  HAL_UARTEx_ReceiveToIdle_IT(&hlpuart1, rx, MESSAGE_MAX); /* start listening */
/* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if (message_len != 0U) {               /* got mail? */
      handle_message(message, message_len);
      message_len = 0U;                    /* box is empty again */
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  GPIO_InitTypeDef led = {0};              /* PA5 (LD2) as an output */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  led.Pin = GPIO_PIN_5;
  led.Mode = GPIO_MODE_OUTPUT_PP;
  led.Pull = GPIO_NOPULL;
  led.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &led);
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
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
