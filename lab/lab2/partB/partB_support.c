/* Part B support: the code that CubeMX generates from lab02_partB.ioc
 * in stm32g4xx_it.c and stm32g4xx_hal_msp.c. This project was generated
 * from the Part A .ioc, so these pieces are compiled only for B1, B2, B3
 * (see LAB_TASK in CMakeLists.txt).
 */
#include "main.h"

extern UART_HandleTypeDef hlpuart1;   /* defined in lab2partBtaskN.c */

/* LPUART1 global interrupt: the HAL receives the bytes and calls
   HAL_UARTEx_RxEventCallback() / HAL_UART_ErrorCallback(). */
void LPUART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&hlpuart1);
}

/* Called by HAL_UART_Init(): clock, pins PA2/PA3 (AF12), interrupt. */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  if (huart->Instance == LPUART1)
  {
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPUART1;
    PeriphClkInit.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_PCLK1;  /* 170 MHz */
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_LPUART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA2: LPUART1_TX, PA3: LPUART1_RX (to the ST-LINK virtual COM port) */
    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF12_LPUART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(LPUART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(LPUART1_IRQn);
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart)
{
  if (huart->Instance == LPUART1)
  {
    __HAL_RCC_LPUART1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2 | GPIO_PIN_3);
    HAL_NVIC_DisableIRQ(LPUART1_IRQn);
  }
}
