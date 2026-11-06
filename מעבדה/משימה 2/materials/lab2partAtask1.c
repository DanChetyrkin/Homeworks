/* Lab 2, Part A, Task A1: send text and random digits.
   No HAL here: we talk to the registers ourselves.
   Paste each block into the same USER CODE place in main.c. */

/* USER CODE BEGIN PD */
#define LPUART1_BRR_VALUE 377778UL   /* speed: 256 * 170 MHz / 115200 */
/* USER CODE END PD */

/* USER CODE BEGIN PV */
static uint32_t previous = 0;        /* was B1 down last time? */
/* USER CODE END PV */

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

/* USER CODE BEGIN 2 */
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOCEN;   /* wake up port A and C */
  GPIOC->MODER &= ~(3UL << 26);                                /* PC13 (B1): input 00 */
  GPIOA->MODER = (GPIOA->MODER & ~(3UL << 10)) | (1UL << 10);  /* PA5 (LD2): output 01 */
  uart_init();
  rng_init();
  uart_puts("Lab 2 ready.\r\n");                               /* say hello */
/* USER CODE END 2 */

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
