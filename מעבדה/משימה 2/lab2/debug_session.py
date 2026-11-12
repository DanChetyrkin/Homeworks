#!/usr/bin/env python3
"""Lab 2: the debugger part of each task, run with GDB on the board.

It uses the same debugger as VS Code (ST-LINK_gdbserver + arm-none-eabi-gdb).
For each task it flashes this project's build, attaches GDB, pauses the
program, evaluates the WATCH expressions of the task, and uses breakpoints to
show what the code does. While GDB waits at a "continue", the script sends
the characters or Modbus requests over COM7, so the board reacts live.

Each transcript is saved in ../captures/<task>/debug-<task>.txt:
    (gdb) lines     what GDB printed
    >>> lines       bytes the PC sent to the board
    <<< lines       bytes the board sent back

    python debug_session.py              all tasks
    python debug_session.py A3b B1       only these tasks
    python debug_session.py --port COM7

Stop the Serial Monitor (kill its COM7 terminal) and any VS Code debug
session first: each needs the port or the ST-LINK.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CAPTURES = os.path.join(ROOT, 'captures')
SRC = os.path.join(HERE, 'Core', 'Src')
RUN_TASK = os.path.join(HERE, 'run-task.ps1')
BUNDLES = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'stm32cube', 'bundles')
WORK = os.path.join(tempfile.gettempdir(), 'lab2-debug')   # ASCII path for GDB
TCP_PORT = 61234
SEND = '@@SEND@@'          # GDB echoes this: the script sends the next item
WAIT_HALTED = 'shell ping -n 3 127.0.0.1 > nul'   # GDB waits about 2 s
TASKS = ['A1', 'A2', 'A3b', 'A3c', 'B1', 'B2', 'B3']
FOLDER = {'A1': 'A1', 'A2': 'A2', 'A3b': 'A3', 'A3c': 'A3', 'B1': 'B1', 'B2': 'B2', 'B3': 'B3'}


def newest(pattern):
    hits = sorted(glob.glob(os.path.join(BUNDLES, pattern)), reverse=True)
    if not hits:
        sys.exit('Not found in the STM32Cube bundles: %s' % pattern)
    return hits[0]


GDBSERVER = newest(os.path.join('stlink-gdbserver', '*', 'bin', 'ST-LINK_gdbserver.exe'))
GDB = newest(os.path.join('gnu-gdb-for-stm32', '*', 'bin', 'arm-none-eabi-gdb.exe'))
PROGRAMMER_BIN = os.path.dirname(newest(os.path.join('programmer', '*', 'bin', 'STM32_Programmer_CLI.exe')))


def line_of(filename, text, after=0, before=10 ** 9):
    """Line number of the first line with text, between after and before."""
    with open(os.path.join(SRC, filename), encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            if text in line and after < n < before:
                return n
    raise RuntimeError('"%s" not found in %s' % (text, filename))


# ---------------------------------------------------------------- Modbus frames
def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def frame(*body):
    data = bytes(body)
    crc = crc16(data)
    return data + bytes([crc & 0xFF, crc >> 8])


WRITE_LD2_ON = frame(1, 0x06, 0, 0, 0, 1)        # 01 06 00 00 00 01 48 0A
WRITE_LD2_OFF = frame(1, 0x06, 0, 0, 0, 0)
READ_ALL = frame(1, 0x03, 0, 0, 0, 5)            # 01 03 00 00 00 05 85 C9
READ_REG9 = frame(1, 0x03, 0, 9, 0, 1)
FUNCTION_04 = frame(1, 0x04, 0, 0, 0, 1)
OTHER_SLAVE = frame(2, 0x03, 0, 0, 0, 5)         # matching CRC, slave 2
NOISY = bytes([READ_ALL[0] ^ 0x02]) + READ_ALL[1:]   # address bit 1 flipped: 03 ...


def hexs(data):
    return ' '.join('%02X' % b for b in data)


# ---------------------------------------------------------------- GDB helpers
def watch(expr, hexa=False):
    """A WATCH line: the expression exactly as typed in VS Code, and its value."""
    if hexa:
        return 'printf "WATCH  %-36s = %%u = 0x%%X\\n", %s, %s' % (expr, expr, expr)
    return 'printf "WATCH  %-36s = %%u\\n", %s' % (expr, expr)


def title(text):
    return 'echo \\n----- %s\\n' % text.replace('"', "'")


RXNE = '(LPUART1->ISR >> 5) & 1U'
ORE = '(LPUART1->ISR >> 3) & 1U'
REGS = ('printf "regs = { LD2 %u, good %u, bad %u, crc_sw %u, crc_hw %u }\\n", '
        'regs[0], regs[1], regs[2], regs[3], regs[4]')


class Transcript:
    def __init__(self, task):
        self.task = task
        self.lines = []
        self.lock = threading.Lock()
        self.problems = []

    def __call__(self, text):
        with self.lock:
            print(text, flush=True)
            self.lines.append(text)

    def save(self):
        path = os.path.join(CAPTURES, FOLDER[self.task], 'debug-%s.txt' % self.task)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(self.lines) + '\n')
        print('  saved %s' % os.path.relpath(path, ROOT))


def clean(line):
    return re.sub(r'[A-Za-z]:[/\\]Users[/\\].*?[/\\]lab2[/\\]', '', line)


def flash(task, log):
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', RUN_TASK, task],
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode != 0:
        log((r.stdout + r.stderr)[-1500:])
        raise RuntimeError('build/flash of %s failed' % task)
    log('flashed build/%s/lab2.elf (this project, Debug -O0 -g3); the board runs it from reset' % task)


def start_server():
    logpath = os.path.join(WORK, 'gdbserver.log')
    logfile = open(logpath, 'w')
    server = subprocess.Popen([GDBSERVER, '-p', str(TCP_PORT), '-l', '1', '-d', '-g', '-m', '0',
                               '-cp', PROGRAMMER_BIN], stdout=logfile, stderr=subprocess.STDOUT)
    end = time.time() + 20
    while time.time() < end:
        with open(logpath, errors='replace') as f:
            if 'Waiting for debugger connection' in f.read():
                return server, logfile
        if server.poll() is not None:
            break
        time.sleep(0.2)
    server.kill()
    logfile.close()
    raise RuntimeError('ST-LINK_gdbserver did not start; see %s' % logpath)


def serial_reader(port, log, stop, as_text):
    data = b''
    last = time.time()
    while not stop.is_set():
        chunk = port.read(64)
        if chunk:
            data += chunk
            last = time.time()
        elif data and time.time() - last > 0.05:
            shown = repr(data.decode(errors='replace')) if as_text else hexs(data)
            log('<<< [board -> PC] %s' % shown)
            data = b''


def run_gdb(task, commands, port, log, sends=(), as_text=True, timeout=120):
    """Attach GDB to the running program, run commands, and detach (the program runs on)."""
    os.makedirs(WORK, exist_ok=True)
    elf = os.path.join(WORK, '%s.elf' % task)
    shutil.copyfile(os.path.join(HERE, 'build', task, 'lab2.elf'), elf)
    script = os.path.join(WORK, '%s.gdb' % task)
    with open(script, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(['set pagination off', 'set confirm off', 'set width 0',
                           'set print pretty off',
                           'echo \\n===== GDB attaches: the program PAUSES here (like Pause, F6, in VS Code)\\n',
                           'target remote localhost:%d' % TCP_PORT]
                          + commands
                          + ['delete', 'echo \\n===== detach: the program RUNS again (like Continue, F5)\\n',
                             'detach']) + '\n')

    pending = list(sends)
    stop = threading.Event()
    reader = threading.Thread(target=serial_reader, args=(port, log, stop, as_text), daemon=True)
    reader.start()
    server, server_log = start_server()
    gdb = None
    try:
        gdb = subprocess.Popen([GDB, '-q', '-batch', '-nx', '-x', script, elf], cwd=WORK,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

        def fire(data, note):
            time.sleep(0.6)
            port.write(data)
            port.flush()
            log('>>> [PC -> board] %s: %s' % (note, repr(data.decode()) if as_text else hexs(data)))

        for raw in iter(gdb.stdout.readline, b''):
            line = clean(raw.decode('utf-8', 'replace').rstrip('\r\n'))
            if line.strip() == SEND:
                if pending:
                    data, note = pending.pop(0)
                    threading.Thread(target=fire, args=(data, note), daemon=True).start()
                continue
            log(line)
        gdb.wait(timeout)
        time.sleep(0.6)      # let the last reply arrive
        if gdb.returncode != 0:
            log.problems.append('gdb exit status %d' % gdb.returncode)
    except subprocess.TimeoutExpired:
        log.problems.append('gdb timed out')
        gdb.kill()
    finally:
        stop.set()
        reader.join(1)
        server.terminate()
        try:
            server.wait(5)
        except subprocess.TimeoutExpired:
            server.kill()
        server_log.close()
    if pending:
        log.problems.append('%d send(s) not done' % len(pending))


# ---------------------------------------------------------------- the tasks
def task_A1(port, log):
    flash('A1', log)
    time.sleep(0.5)
    run_gdb('A1', [
        'frame',
        title('WATCH of Task A1: the mode and AF fields of PA2 (low half) and PA3 (high half)'),
        watch('(GPIOA->MODER >> 4) & 0xF', hexa=True),
        watch('(GPIOA->AFR[0] >> 8) & 0xFF', hexa=True),
        'printf "-> mode PA3 = %u%u, PA2 = %u%u (10 = alternate function)\\n", '
        '((GPIOA->MODER >> 7) & 1), ((GPIOA->MODER >> 6) & 1), ((GPIOA->MODER >> 5) & 1), ((GPIOA->MODER >> 4) & 1)',
        'printf "-> AF   PA3 = %u, PA2 = %u (AF12 = LPUART1)\\n", (GPIOA->AFR[0] >> 12) & 0xF, (GPIOA->AFR[0] >> 8) & 0xF',
        title('the rest of uart_init() and the GPIO setup'),
        'printf "GPIOA->MODER = 0x%08X   GPIOA->AFR[0] = 0x%08X\\n", GPIOA->MODER, GPIOA->AFR[0]',
        watch('(GPIOA->MODER >> 26) & 3'),
        'echo   -> PA13 = 10 (SWDIO): uart_init() kept the debugger pins\\n',
        watch('(GPIOA->MODER >> 10) & 3'),
        'echo   -> PA5 (LD2) = 01, output\\n',
        watch('(GPIOC->MODER >> 26) & 3'),
        'echo   -> PC13 (B1) = 00, input\\n',
        watch('RCC->APB1ENR2 & 1'),
        'echo   -> LPUART1EN = 1, the clock of LPUART1 is on\\n',
        watch('LPUART1->BRR'),
        watch('LPUART1->CR1', hexa=True),
        'echo   -> 0xD = UE (bit 0) + RE (bit 2) + TE (bit 3)\\n',
        watch('(GPIOC->IDR >> 13) & 1'),
        'printf "previous = %u   (B1 state of the last pass)\\n", previous',
    ], port, log)


def task_A2(port, log):
    flash('A2', log)
    time.sleep(0.5)
    run_gdb('A2', [
        'frame',
        title('USART1 copy on PC4 (Task A2)'),
        watch('(GPIOC->MODER >> 8) & 3'),
        'echo   -> PC4 = 10, alternate function\\n',
        watch('(GPIOC->AFR[0] >> 16) & 0xF'),
        'echo   -> AF7 = USART1_TX\\n',
        watch('(RCC->APB2ENR >> 14) & 1'),
        'echo   -> USART1EN = 1\\n',
        watch('USART1->BRR'),
        'printf "-> baud = 170 MHz / %u = %.1f\\n", USART1->BRR, 170000000.0 / USART1->BRR',
        watch('LPUART1->BRR'),
        'printf "-> baud = 256 * 170 MHz / %u = %.1f\\n", LPUART1->BRR, 256.0 * 170000000.0 / LPUART1->BRR',
        watch('USART1->CR1', hexa=True),
        'echo   -> 0x9 = UE + TE; RE (bit 2) = 0: USART1 only transmits\\n',
    ], port, log)


def task_A3b(port, log):
    f = 'lab2partAtask3.c'
    main = line_of(f, 'int main(void)')
    echo_line = line_of(f, 'uart_putc((char)LPUART1->RDR);', after=main)
    toggle = line_of(f, 'GPIOA->ODR ^= GPIO_ODR_OD5;', after=main)
    flash('A3b', log)
    time.sleep(0.5)
    run_gdb('A3b', [
        'frame',
        'info locals',
        'echo   (the pause almost always lands in the busy-wait loop: i counts to WORK_PASSES = 2000000)\\n',
        title('fresh start: nothing received yet'),
        watch(RXNE), watch(ORE),
        title('breakpoint on the polling echo, line %d; the PC sends 0123456789 while the loop is in the delay' % echo_line),
        'break %s:%d' % (f, echo_line),
        'echo %s\\n' % SEND,
        'continue',
        title('stopped at the echo: after the delay, the loop found RXNE = 1'),
        watch(RXNE), watch(ORE),
        'echo   -> RXNE = 1: RDR holds the first character. ORE = 1: the other 9 overran and are lost\\n',
        'printf "LPUART1->ISR = 0x%08X\\n", LPUART1->ISR',
        'next',
        title('after this line ran: it read RDR and echoed it'),
        watch(RXNE), watch(ORE),
        'echo   -> reading RDR cleared RXNE. ORE stays 1: the polling code never clears it\\n',
        'delete',
        title('the PC sends 0123456789 again; then pause after 6 more loop passes (about 1 s)'),
        'break %s:%d' % (f, toggle),
        'ignore $bpnum 6',
        'echo %s\\n' % SEND,
        'continue',
        title('WATCH of Task A3b: paused after the two lines'),
        watch(RXNE), watch(ORE),
        'printf "LPUART1->ISR = 0x%08X\\n", LPUART1->ISR',
        'echo   -> RXNE = 0 and ORE = 1: while ORE is set, no new byte reaches RDR, so the second line returns nothing\\n',
    ], port, log, sends=[(b'0123456789', 'first line'), (b'0123456789', 'second line')])


def task_A3c(port, log):
    f = 'lab2partAtask3.c'
    main = line_of(f, 'int main(void)')
    echo_irq = line_of(f, 'uart_putc((char)LPUART1->RDR);', before=main)
    flash('A3c', log)
    time.sleep(0.5)
    run_gdb('A3c', [
        'frame',
        title('the receive interrupt is enabled'),
        watch('(LPUART1->CR1 >> 5) & 1U'),
        'echo   -> RXNEIE = 1\\n',
        watch('(NVIC->ISER[2] >> 27) & 1U'),
        'echo   -> NVIC enable of IRQ 91 = 64 + 27 (LPUART1)\\n',
        title('breakpoint on LPUART1_IRQHandler; the PC sends one character while the loop is in the delay'),
        'break LPUART1_IRQHandler',
        'echo %s\\n' % SEND,
        'continue',
        title('stopped in the interrupt handler'),
        'printf "IPSR = %u = 16 + %u -> exception of IRQ 91, LPUART1\\n", $xpsr & 0x1FF, ($xpsr & 0x1FF) - 16',
        watch(RXNE), watch(ORE),
        'echo   -> the character is in RDR (RXNE = 1), no overrun\\n',
        'echo \\nThe call stack: the interrupt stopped main() inside the busy-wait delay (see i):\\n',
        'bt full',
        'delete',
        'break %s:%d' % (f, echo_irq),
        'continue',
        'next',
        title('after the echo line of the handler'),
        watch(RXNE),
        'echo   -> RDR was read within microseconds, long before the next frame (86.8 us) ends\\n',
    ], port, log, sends=[(b'A', 'one character')])


def task_B1(port, log):
    f = 'lab2partBtask1.c'
    main = line_of(f, 'int main(void)')
    loop_check = line_of(f, 'if (message_len != 0U)', after=main)
    flash('B1', log)
    time.sleep(0.5)
    run_gdb('B1', [
        'frame',
        'printf "message_len = %u  (0: the buffer is free)\\n", message_len',
        REGS,
        title('breakpoints on the reception callback and on handle_message; the PC writes LD2 = 1'),
        'break HAL_UARTEx_RxEventCallback',
        'break handle_message',
        'echo %s\\n' % SEND,
        'continue',
        title('1) HAL_UARTEx_RxEventCallback runs INSIDE the LPUART1 interrupt, when the line goes idle'),
        'bt',
        'printf "size = %u bytes; message_len = %u (free, so the callback copies rx into message)\\n", size, message_len',
        'x/8xb rx',
        'continue',
        title('2) the main loop calls handle_message with the copy'),
        'bt',
        'printf "message_length = %u; message_len = %u (occupied: a new message now would be thrown away)\\n", '
        'message_length, message_len',
        'x/8xb message_bytes',
        'printf "address %u, function 0x%02X, register %u, value %u\\n", message_bytes[0], message_bytes[1], '
        '(message_bytes[2] << 8) | message_bytes[3], (message_bytes[4] << 8) | message_bytes[5]',
        title('3) error recovery: the CPU stays paused while the PC sends another request -> overrun'),
        'delete',
        'break HAL_UART_ErrorCallback',
        'echo %s\\n' % SEND,
        WAIT_HALTED,
        watch(ORE),
        'echo   -> ORE = 1: nobody read RDR while the CPU was stopped\\n',
        'continue',
        title('HAL_UART_ErrorCallback: the HAL stopped reception because of the overrun'),
        'bt',
        'printf "huart->ErrorCode = 0x%X  (0x8 = HAL_UART_ERROR_ORE)\\n", huart->ErrorCode',
        'printf "huart->RxState = 0x%X  (0x20 = ready: reception is stopped)\\n", huart->RxState',
        'next',
        'next',
        'printf "after HAL_UARTEx_ReceiveToIdle_IT: huart->RxState = 0x%X  (0x22 = receiving again)\\n", huart->RxState',
        'delete',
        'tbreak %s:%d' % (f, loop_check),
        'continue',
        title('back in the loop: the first request was handled and answered'),
        REGS,
        watch('(GPIOA->ODR >> 5) & 1U'),
        'echo   -> LD2 is on\\n',
        'printf "message_len = %u  (cleared after handle_message returned: free again)\\n", message_len',
        title('reception works again: the PC writes LD2 = 0'),
        'break handle_message',
        'echo %s\\n' % SEND,
        'continue',
        'x/8xb message_bytes',
        'finish',
        REGS,
        watch('(GPIOA->ODR >> 5) & 1U'),
        'echo   -> LD2 is off; good = 2: the request lost in the overrun was never received\\n',
    ], port, log, sends=[(WRITE_LD2_ON, 'write LD2 = 1'),
                         (WRITE_LD2_ON, 'write LD2 = 1 while the CPU is paused'),
                         (WRITE_LD2_OFF, 'write LD2 = 0')], as_text=False)


def task_B2(port, log):
    flash('B2', log)
    time.sleep(0.5)
    run_gdb('B2', [
        'frame',
        REGS,
        title('breakpoint on exception(); the PC reads register 9'),
        'break exception',
        'echo %s\\n' % SEND,
        'continue',
        'bt',
        'printf "exception(function = 0x%02X, code = %u)  -> 02: illegal data address\\n", function, code',
        title('the PC sends function 0x04'),
        'echo %s\\n' % SEND,
        'continue',
        'printf "exception(function = 0x%02X, code = %u)  -> 01: illegal function\\n", function, code',
        'delete',
        title('noise: the PC sends a read whose address bit 1 is flipped (01 -> 03), CRC of the original'),
        'break handle_message',
        'echo %s\\n' % SEND,
        'continue',
        'x/8xb message_bytes',
        'finish',
        REGS,
        'echo   -> bad + 1: the CRC is checked first, so this counts as noise, not as a message for slave 3\\n',
        title('a correct message for slave 2'),
        'echo %s\\n' % SEND,
        'continue',
        'x/8xb message_bytes',
        'finish',
        REGS,
        'echo   -> no change: CRC matches, but the address is not 1 -> ignored, no reply\\n',
        title('a correct read of all 5 registers'),
        'echo %s\\n' % SEND,
        'continue',
        'finish',
        REGS,
        'echo   -> good + 1, and the reply carries these values\\n',
    ], port, log, sends=[(READ_REG9, 'read register 9'), (FUNCTION_04, 'function 0x04'),
                         (NOISY, 'read, address bit flipped'), (OTHER_SLAVE, 'read for slave 2'),
                         (READ_ALL, 'read registers 0-4')], as_text=False)


def task_B3(port, log):
    f = 'lab2partBtask3.c'
    crc_return = line_of(f, 'return received == crc_sw && received == crc_hw;')
    flash('B3', log)
    time.sleep(0.5)
    run_gdb('B3', [
        'frame',
        title('the cycle counter and the CRC unit after the preparation'),
        watch('(CoreDebug->DEMCR >> 24) & 1U'),
        'echo   -> TRCENA = 1\\n',
        watch('DWT->CTRL & 1U'),
        'echo   -> CYCCNTENA = 1: DWT->CYCCNT counts every clock cycle\\n',
        watch('(RCC->AHB1ENR >> 12) & 1U'),
        'echo   -> CRCEN = 1\\n',
        watch('CRC->POL', hexa=True),
        watch('CRC->INIT', hexa=True),
        watch('CRC->CR', hexa=True),
        'printf "-> POLYSIZE = %u (01: 16 bits), REV_IN = %u (01: by byte), REV_OUT = %u\\n", '
        '(CRC->CR >> 3) & 3, (CRC->CR >> 5) & 3, (CRC->CR >> 7) & 1',
        title('breakpoint at the end of crc_ok (line %d); the PC reads registers 0-4' % crc_return),
        'break %s:%d' % (f, crc_return),
        'echo %s\\n' % SEND,
        'continue',
        'x/8xb message_bytes',
        'printf "received = 0x%04X, crc_sw = 0x%04X, crc_hw = 0x%04X  (same: the CRC matches)\\n", '
        'received, crc_sw, crc_hw',
        'printf "byte_count = %u CRC input bytes\\n", byte_count',
        watch('regs[3]'),
        watch('regs[4]'),
        'printf "-> software %.2f us, hardware %.3f us at 170 MHz; ratio %.2f\\n", '
        'regs[3] / 170.0, regs[4] / 170.0, (double) regs[3] / regs[4]',
    ], port, log, sends=[(READ_ALL, 'read registers 0-4')], as_text=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tasks', nargs='*', default=TASKS)
    ap.add_argument('--port', default='COM7')
    a = ap.parse_args()
    unknown = [t for t in a.tasks if t not in TASKS]
    if unknown:
        ap.error('unknown task(s): %s (choose from %s)' % (', '.join(unknown), ' '.join(TASKS)))
    try:
        port = serial.Serial(a.port, 115200, timeout=0.02)
    except serial.SerialException as e:
        sys.exit('Cannot open %s: %s\nKill the Serial Monitor terminal (COM7), then run again.' % (a.port, e))

    results = []
    with port:
        for task in a.tasks:
            log = Transcript(task)
            log('===== Lab 2, %s: debugger session %s' % (task, time.strftime('%Y-%m-%d %H:%M:%S')))
            try:
                globals()['task_' + task](port, log)
            except Exception as e:  # keep going with the next task
                log.problems.append(str(e))
            for p in log.problems:
                log('PROBLEM: %s' % p)
            log.save()
            results.append((task, not log.problems))
            print()
    print('===== summary')
    for task, ok in results:
        print('  %-4s %s' % (task, 'done' if ok else 'PROBLEM (see its transcript)'))
    return 0 if all(ok for _, ok in results) else 1


if __name__ == '__main__':
    sys.exit(main())
