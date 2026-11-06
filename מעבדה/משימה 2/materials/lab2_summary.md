# Lab 2 – "The Noisy Channel": simple summary

## What we learn

### UART: how 2 chips send letters

- 2 wires: **TX** (I talk) and **RX** (I listen). There is no clock wire, so both sides must use the **same speed** (baud rate).
- **8N1 frame:** the line rests at 1, then a **start bit** (0), 8 data bits (**smallest bit first**), then a **stop bit** (1).
  - That is 10 bits per letter.
  - At 115200 baud: 1 bit = 8.68 µs, 1 letter = 86.8 µs.
- The UART just moves bytes. What they mean is our job.

### Registers (talking to the chip directly)

| Register | What it does                                                               |
| -------- | -------------------------------------------------------------------------- |
| `CR1`    | on/off (UE), talk (TE), listen (RE), "tap me" interrupt (RXNEIE)           |
| `BRR`    | speed (only change it while UE = 0)                                        |
| `ISR`    | flags: TXE = box empty, RXNE = letter arrived, ORE = letter lost (overrun) |
| `ICR`    | write 1 here to clear a flag                                               |
| `TDR`    | send box                                                                   |
| `RDR`    | receive box                                                                |

- A pin can do a **special job** (alternate function): MODER = 10, plus the job number in AFR.
- Every part needs its **clock turned on** in RCC first.

### Polling vs interrupt

- **Polling:** the loop keeps asking "did a letter come?". If the loop is busy, letters get lost (ORE).
- **Interrupt:** the chip stops main and runs a handler right away, so nothing is lost.
  - It needs 3 things: enable in the part, enable in the NVIC, and a handler with the exact name.
  - The handler must clear its flag, or it runs forever.
  - A smaller priority number wins.

### RNG

- A real random number maker. Wait for DRDY, then read DR.

### Messages and CRC

- We need to know where a message ends. Modbus RTU uses **silence** (3.5 letter times).
- **CRC** is a "check number" added to the end. The receiver computes it again. If it is different, noise broke the message.
- CRC-16/MODBUS: polynomial 0x8005 (0xA001 reversed), starts at 0xFFFF, sent **low byte first**.
- Bad CRC: say **nothing**. The master waits 0.2 s and sends again.

### HAL reception

- `HAL_UARTEx_ReceiveToIdle_IT` listens until the line goes quiet. It then calls `HAL_UARTEx_RxEventCallback`.
- The callback copies the message into a box, and the main loop handles it.
- After an error, `HAL_UART_ErrorCallback` must restart listening.

### Modbus RTU

- Message layout: **address, function, data, CRC**.
- **0x06** = write 1 register. The answer is a copy of the request.
- **0x03** = read registers. The answer is a byte count, then the values.
- **Exception** = function + 0x80, then a code: **01** = I can't do that function, **02** = no such register, **03** = bad value.

### CRC unit and cycle counter

- **CRC unit:** hardware that computes a CRC in 1 clock per byte.
- **`DWT->CYCCNT`:** a stopwatch that counts clock cycles (170 per µs).

## The tasks (what each file does)

| Task | File               | What it does                                                                                                        |
| ---- | ------------------ | ------------------------------------------------------------------------------------------------------------------- |
| A1   | `lab2partAtask1.c` | says "Lab 2 ready.", sends a random digit on each B1 press (registers only)                                         |
| A2   | `lab2partAtask2.c` | same, but also sends on USART1 / PC4 so the scope can see it                                                        |
| A3   | `lab2partAtask3.c` | echoes letters back. A slow delay loop makes polling lose letters, and the interrupt fixes it (`ECHO_BY_INTERRUPT`) |
| B1   | `lab2partBtask1.c` | Modbus slave: the PC switches LD2 on/off (0x06)                                                                     |
| B2   | `lab2partBtask2.c` | + read registers (0x03), + exception replies                                                                        |
| B3   | `lab2partBtask3.c` | + hardware CRC unit, and counts the cycles of software CRC vs hardware CRC                                          |

**How to use the files:** each file holds only the `USER CODE` blocks. Paste each block into the same `USER CODE BEGIN …` place in the CubeMX `main.c`.

- Part A: new project, 170 MHz clock.
- Part B: the project from `lab02_partB.ioc`.
- A3: if `stm32g4xx_it.c` already has `LPUART1_IRQHandler`, delete that one.

## Do at HOME (done / to fill)

### Pre-lab answers

1. **Bit numbers:**
   - CR1: UE = 0, RE = 2, TE = 3, RXNEIE = 5.
   - ISR: ORE = 3, RXNE = 5, TXE = 7.
   - Clock: `RCC_APB1ENR2` bit 0 (LPUART1EN).
2. **Speed, using 115200:**
   - LPUART1 BRR = 256·170 MHz / 115200 ≈ **377778**.
   - USART1 BRR ≈ **1476**.
   - Bit = 8.68 µs, frame = 86.8 µs.
3. **Alternate function numbers:** PA2/PA3 = **AF12** (LPUART1). PC4 = **AF7** (USART1_TX).
4. **Interrupt:** LPUART1 = position **91**, `LPUART1_IRQHandler`, `NVIC->ISER[2]` bit **27**.
5. **Time budget:**
   - 1 byte = 86.8 µs, 8-byte request = 694 µs.
   - 3.5 letter times = 304 µs.
   - 15-byte reply = 1.30 ms.
6. **Decode `01 06 00 00 00 01 48 0A`:**
   - Address 1, function 0x06 (write), register 0, value 1, CRC = **0x0A48**.
   - If the last byte becomes 0x0B: the CRC is wrong, so the board sends **no reply** (`bad`++) and the master sends again.

### Predictions you can think about at home

- **B2(a):** read of register 9 gives code **02**. Function 0x04 gives code **01**.
- **B2(g):** about 0.7 + 0.3 + 1.3 + 0.3 ≈ **2.6 ms**. That is much less than 0.2 s, because the PC and USB add delay.
- **B3(a):**
  - Software does about 8 loops × a few instructions per byte, so a few hundred cycles for 6 bytes.
  - Hardware is about 6 cycles plus register writes and reads.
  - So the hardware should be several times faster.

## Do at SCHOOL (needs the board / scope, no code)

- **A1:**
  - Serial Monitor test (line ending **None**, 115200).
  - Debugger watches on `GPIOA->MODER` and `AFR[0]`.
- **A2 (oscilloscope):**
  - Capture the PC4 frame of one B1 press (save the image).
  - Measure 1 bit time and the start-to-stop time (9 bits).
  - Calculate the baud rate and decode the 8 bits.
- **A3:**
  - Send `0123456789` with the polling version and the interrupt version.
  - Scope on PA5 to measure the delay (save the image).
  - Watch RXNE and ORE in the debugger.
- **B1 to B3:** stop the Serial Monitor first, then run:
  - B1: `python lab02_modbus.py --write-only`
  - B2: `python lab02_modbus.py`, then `--exceptions`, then reset and `--errors 0.3` (compare `bad` with "with a wrong bit").
  - B3: read `crc_sw` and `crc_hw`, convert them to µs (÷170), then compare the ratio and the time per byte.
