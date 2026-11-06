/* Lab 2, Part B, Task B2: also read registers (function 0x03),
   and say "no" (exception) to bad requests.
   Project from lab02_partB.ioc. Here we may use the HAL.
   Paste each block into the same USER CODE place in main.c. */

/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

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

/* USER CODE BEGIN PV */
static uint8_t rx[MESSAGE_MAX];           /* the bytes that arrive */
static uint8_t message[MESSAGE_MAX];      /* a complete message */
static volatile uint16_t message_len = 0; /* its bytes; 0: none */
static uint16_t regs[NUM_REGS];           /* the holding registers */
/* USER CODE END PV */

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
  (void)huart;
  if (message_len == 0U) {                 /* is the loop's box empty? */
    memcpy(message, rx, size);             /* yes: copy the message in */
    message_len = size;                    /* tell the loop "you have mail" */
  }                                        /* no: throw the new one away */
  HAL_UARTEx_ReceiveToIdle_IT(&hlpuart1, rx, MESSAGE_MAX); /* listen again */
}

/* Something went wrong (like overrun). The HAL stopped, so start again. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  (void)huart;
  HAL_UARTEx_ReceiveToIdle_IT(&hlpuart1, rx, MESSAGE_MAX);
}

/* 1 if the last 2 bytes (low byte first) are the right CRC */
static int crc_ok(const uint8_t *message_bytes, uint16_t message_length)
{
  uint16_t crc = modbus_crc16(message_bytes, message_length - 2U);
  return message_bytes[message_length - 2U] == (crc & 0xFFU)
      && message_bytes[message_length - 1U] == (crc >> 8);
}

/* Add the CRC (low byte first) after the bytes, then send it all.
   The array needs 2 free bytes at the end for the CRC. */
static void reply(uint8_t *reply_bytes, uint16_t length_without_crc)
{
  uint16_t crc = modbus_crc16(reply_bytes, length_without_crc);
  reply_bytes[length_without_crc]      = (uint8_t)(crc & 0xFFU);
  reply_bytes[length_without_crc + 1U] = (uint8_t)(crc >> 8);
  HAL_UART_Transmit(&hlpuart1, reply_bytes, length_without_crc + 2U, 100U);
}

/* Say "no": function with bit 7 on, and a code that tells why */
static void exception(uint8_t function, uint8_t code)
{
  uint8_t r[5] = { MB_ADDRESS, (uint8_t)(function | 0x80U), code };
  reply(r, 3U);
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

  uint8_t f = m[1];
  if (f != 0x03U && f != 0x06U) { exception(f, 1U); return; } /* 01: I can't do that */
  if (message_length != 8U)     { exception(f, 3U); return; } /* 03: bad request */

  uint16_t reg   = (uint16_t)((m[2] << 8) | m[3]);   /* high byte first */
  uint16_t value = (uint16_t)((m[4] << 8) | m[5]);

  if (f == 0x06U) {                                  /* write one register */
    if (reg != REG_LD2) { exception(f, 2U); return; } /* 02: only LD2 can be written */
    regs[REG_LD2] = (value != 0U) ? 1U : 0U;         /* 0 = off, else on */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, regs[REG_LD2] ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)m, 8U, 100U); /* reply = copy of request */
    return;
  }

  /* f == 0x03: read "value" registers, starting at "reg" */
  if (value == 0U || value > 125U)   { exception(f, 3U); return; } /* 03: bad count */
  if (reg + value > NUM_REGS)        { exception(f, 2U); return; } /* 02: no such register */

  uint8_t r[3U + 2U * NUM_REGS + 2U];                /* head + values + CRC */
  r[0] = MB_ADDRESS;
  r[1] = 0x03U;
  r[2] = (uint8_t)(2U * value);                      /* how many bytes follow */
  for (uint16_t i = 0; i < value; i++) {
    r[3U + 2U * i] = (uint8_t)(regs[reg + i] >> 8);  /* high byte first */
    r[4U + 2U * i] = (uint8_t)(regs[reg + i] & 0xFFU);
  }
  reply(r, (uint16_t)(3U + 2U * value));
}
/* USER CODE END 0 */

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  GPIO_InitTypeDef led = {0};              /* PA5 (LD2) as an output */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  led.Pin = GPIO_PIN_5;
  led.Mode = GPIO_MODE_OUTPUT_PP;
  led.Pull = GPIO_NOPULL;
  led.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &led);
  /* USER CODE END MX_GPIO_Init_2 */

/* USER CODE BEGIN 2 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* start the cycle counter */
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  HAL_NVIC_SetPriority(LPUART1_IRQn, 1, 0);        /* SysTick (0) may interrupt us */
  HAL_UARTEx_ReceiveToIdle_IT(&hlpuart1, rx, MESSAGE_MAX); /* start listening */
/* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if (message_len != 0U) {               /* got mail? */
      handle_message(message, message_len);
      message_len = 0U;                    /* box is empty again */
    }
    /* USER CODE END WHILE */
