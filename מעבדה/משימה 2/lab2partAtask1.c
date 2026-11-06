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

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LPUART1_BRR_VALUE 377778UL   /* speed: 256 * 170 MHz / 115200 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint32_t previous = 0;        /* was B1 down last time? */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* RNG: 32-bit random numbers from the true random number generator */
static void rng_init(void)
{
  RCC->CRRCR |= RCC_CRRCR_HSI48ON;             /* start HSI48 */
  while ((RCC->CRRCR & RCC_CRRCR_HSI48RDY) == 0U) { }
  RCC->CCIPR &= ~RCC_CCIPR_CLK48SEL;           /* 00: 48 MHz = HSI48 */
  RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;           /* RNG clock on */
  RNG->CR |= RNG_CR_RNGEN;                     /* start the RNG */
}

static uint32_t rng_read(void)                  /* wait for a number */
{
  while ((RNG->SR & RNG_SR_DRDY) == 0U) {
    if ((RNG->SR & RNG_SR_SEIS) != 0U) {        /* seed error */
      RNG->SR &= ~RNG_SR_SEIS;                  /* clear SEIS */
      for (uint32_t i = 0; i < 12U; i++) {      /* discard 12 words */
        (void)RNG->DR;
      }
    }
  }
  return RNG->DR;
}

static void uart_init(void)            /* LPUART1: 115200, 8N1 */
{
  RCC->APB1ENR2 |= RCC_APB1ENR2_LPUART1EN;              /* wake up LPUART1 */

  /* PA2, PA3: mode 10 = "do a special job" */
  GPIOA->MODER = (GPIOA->MODER & ~(0xFUL << 4)) | (0xAUL << 4);
  /* the special job is number 12 = LPUART1 (4 bits each pin) */
  GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(0xFFUL << 8)) | (0xCCUL << 8);

  LPUART1->CR1 = 0;                                     /* off, so we may set speed */
  LPUART1->BRR = LPUART1_BRR_VALUE;                     /* set speed */
  LPUART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE; /* talk, listen, on */
}

static void uart_putc(char character) /* sends one character */
{
  while ((LPUART1->ISR & USART_ISR_TXE) == 0U) { }      /* wait for an empty box */
  LPUART1->TDR = (uint8_t)character;                    /* put the letter in the box */
}

static void uart_puts(const char *text) /* sends a string */
{
  while (*text != '\0') {
    uart_putc(*text++);                                 /* one letter at a time */
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
  /* USER CODE BEGIN 2 */
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOCEN;   /* wake up port A and C */
  GPIOC->MODER &= ~(3UL << 26);                                /* PC13 (B1): input 00 */
  GPIOA->MODER = (GPIOA->MODER & ~(3UL << 10)) | (1UL << 10);  /* PA5 (LD2): output 01 */
  uart_init();
  rng_init();
  uart_puts("Lab 2 ready.\r\n");                               /* say hello */
/* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t now = (GPIOC->IDR >> 13) & 1U;            /* is B1 down now? (1 = down) */
    if (now != 0U && previous == 0U) {                 /* it just went down */
      uart_putc((char)('0' + rng_read() % 10U));       /* send a random digit */
      for (volatile uint32_t i = 0; i < 200000UL; i++) { } /* wait, the button shakes */
    }
    previous = now;                                    /* remember for next time */
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
