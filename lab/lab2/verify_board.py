#!/usr/bin/env python3
"""Lab 2: check every task on the board and save the results in captures/.

For each task it builds and flashes this project's code (run-task.ps1), then:
  A1   reads the startup line, and the WATCH values of PA2/PA3 over ST-LINK
  A2   reads the startup line, PC4 (mode, AF) and both BRR values
  A3a  sends 0123456789 and checks that all 10 return
  A3b  sends 0123456789 two times, then reads RXNE and ORE in LPUART1->ISR
  A3c  sends 0123456789 two times, then reads RXNEIE and the NVIC enable bit
  B1   lab02_modbus.py --write-only
  B2   lab02_modbus.py, --exceptions, then a reset and --errors 0.3
  B3   lab02_modbus.py (crc_sw and crc_hw)

Registers are read in ST-LINK "hot plug" mode: the program keeps running,
and nothing reads RDR, so RXNE is not cleared by the check.

Stop the Serial Monitor first: only one program can open the port.

    python verify_board.py                 all tasks
    python verify_board.py A3b A3c         only these tasks
    python verify_board.py --port COM8          (default: the ST-LINK port, found automatically)
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = HERE  # captures/ and lab02_modbus.py sit next to this script
CAPTURES = os.path.join(ROOT, 'captures')
MODBUS = os.path.join(ROOT, 'lab02_modbus.py')
RUN_TASK = os.path.join(HERE, 'run-task.ps1')
TASKS = ['A1', 'A2', 'A3a', 'A3b', 'A3c', 'B1', 'B2', 'B3']
BURST = b'0123456789'

# Register addresses (RM0440, device header stm32g491xx.h)
GPIOA_MODER = 0x48000000
GPIOA_AFRL = 0x48000020
GPIOC_MODER = 0x48000800
GPIOC_AFRL = 0x48000820
LPUART1_CR1 = 0x40008000
LPUART1_BRR = 0x4000800C
LPUART1_ISR = 0x4000801C
USART1_CR1 = 0x40013800
USART1_BRR = 0x4001380C
NVIC_ISER2 = 0xE000E108


def find_cli():
    base = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'stm32cube', 'bundles', 'programmer')
    hits = sorted(glob.glob(os.path.join(base, '*', 'bin', 'STM32_Programmer_CLI.exe')), reverse=True)
    return hits[0] if hits else 'STM32_Programmer_CLI'


CLI = find_cli()


class Log:
    """Prints each line and keeps it for the log file of the task."""

    def __init__(self, task):
        self.task = task
        self.lines = []
        self.ok = True

    def __call__(self, text=''):
        print(text, flush=True)
        self.lines.append(text)

    def check(self, condition, text):
        self('  [%s] %s' % ('PASS' if condition else 'FAIL', text))
        self.ok = self.ok and bool(condition)

    def save(self, folder, name):
        path = os.path.join(CAPTURES, folder, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(self.lines) + '\n')
        print('  saved %s' % os.path.relpath(path, ROOT))


def flash(task, log):
    log('flash: run-task.ps1 %s' % task)
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', RUN_TASK, task],
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode != 0:
        log((r.stdout + r.stderr)[-2000:])
        raise RuntimeError('build/flash of %s failed' % task)
    elf = os.path.join(HERE, 'build', task, 'lab2.elf')
    log('  flashed %s (built %s)' % (os.path.relpath(elf, ROOT), time.strftime('%Y-%m-%d %H:%M', time.localtime(os.path.getmtime(elf)))))


def cli(*args):
    r = subprocess.run([CLI, '-c', 'port=SWD', 'mode=HOTPLUG'] + list(args),
                       capture_output=True, text=True, errors='replace')
    if r.returncode != 0:
        raise RuntimeError('ST-LINK command failed: %s\n%s' % (' '.join(args), r.stdout[-800:]))
    return r.stdout


def read32(*addrs):
    args = []
    for a in addrs:
        args += ['-r32', '0x%08X' % a, '1']
    out = cli(*args)
    values = {int(a, 16): int(v, 16) for a, v in re.findall(r'0x([0-9A-Fa-f]{8})\s*:\s*([0-9A-Fa-f]{8})', out)}
    missing = [a for a in addrs if a not in values]
    if missing:
        raise RuntimeError('no value read at %s' % ', '.join('0x%08X' % a for a in missing))
    return [values[a] for a in addrs]


def reset_board(log):
    cli('-rst')
    log('reset the board (counters start at zero)')
    time.sleep(0.5)


def read_for(port, seconds):
    end = time.time() + seconds
    data = b''
    while time.time() < end:
        data += port.read(256)
    return data


def open_port(name):
    return serial.Serial(name, 115200, bytesize=8, parity='N', stopbits=1, timeout=0.05)


def flash_and_startup(task, portname, log):
    """Opens the port, flashes (the board resets), and returns the port and the startup text."""
    port = open_port(portname)
    port.reset_input_buffer()
    flash(task, log)
    startup = read_for(port, 1.5)
    log('  startup text: %r' % startup.decode(errors='replace'))
    log.check(b'Lab 2 ready.' in startup, 'the board sends "Lab 2 ready." at reset')
    return port


def burst(port, log, number, seconds=1.0):
    port.reset_input_buffer()
    port.write(BURST)
    back = read_for(port, seconds)
    text = back.decode(errors='replace')
    log('  burst %d: sent %r, received %r (%d/10)' % (number, BURST.decode(), text, len(back)))
    return back


def task_A1(portname, log):
    port = flash_and_startup('A1', portname, log)
    port.close()
    moder, afrl, cmoder, brr, cr1 = read32(GPIOA_MODER, GPIOA_AFRL, GPIOC_MODER, LPUART1_BRR, LPUART1_CR1)
    w1 = (moder >> 4) & 0xF
    w2 = (afrl >> 8) & 0xFF
    log('WATCH (read over ST-LINK, program running):')
    log('  GPIOA->MODER = 0x%08X   GPIOA->AFR[0] = 0x%08X' % (moder, afrl))
    log('  (GPIOA->MODER >> 4) & 0xF    = %d = 0x%X = 0b%s  (PA3 %s | PA2 %s)' % (
        w1, w1, format(w1, '04b'), format(w1 >> 2, '02b'), format(w1 & 3, '02b')))
    log('  (GPIOA->AFR[0] >> 8) & 0xFF  = %d = 0x%02X  (PA3 AF%d | PA2 AF%d)' % (w2, w2, w2 >> 4, w2 & 0xF))
    log.check(w1 == 0xA, 'PA2 and PA3 in mode 10 (alternate function)')
    log.check(w2 == 0xCC, 'PA2 and PA3 on AF12 (LPUART1)')
    log.check(((moder >> 10) & 3) == 1, 'PA5 (LD2) is an output (01)')
    log.check(((cmoder >> 26) & 3) == 0, 'PC13 (B1) is an input (00)')
    log.check(brr == 377778, 'LPUART1->BRR = %d (expected 377778)' % brr)
    log.check((cr1 & 0xD) == 0xD, 'LPUART1->CR1 = 0x%08X: UE, RE, TE set' % cr1)
    log('MANUAL (needs a finger on B1): 10 presses -> 10 digits; hold B1 -> 1 digit.')


def task_A2(portname, log):
    port = flash_and_startup('A2', portname, log)
    port.close()
    cmoder, cafrl, ubrr, ucr1, lbrr = read32(GPIOC_MODER, GPIOC_AFRL, USART1_BRR, USART1_CR1, LPUART1_BRR)
    log('  GPIOC->MODER = 0x%08X   GPIOC->AFR[0] = 0x%08X' % (cmoder, cafrl))
    log.check(((cmoder >> 8) & 3) == 2, 'PC4 in mode 10 (alternate function)')
    log.check(((cafrl >> 16) & 0xF) == 7, 'PC4 on AF%d (USART1_TX is AF7)' % ((cafrl >> 16) & 0xF))
    log.check(ubrr == 1476, 'USART1->BRR = %d (expected 1476)' % ubrr)
    log.check(lbrr == 377778, 'LPUART1->BRR = %d (expected 377778)' % lbrr)
    log.check((ucr1 & 0xD) == 0x9, 'USART1->CR1 = 0x%08X: UE and TE set, RE = 0' % ucr1)
    log('  baud from BRR: LPUART1 256*170e6/377778 = %.1f, USART1 170e6/1476 = %.1f' % (
        256 * 170e6 / 377778, 170e6 / 1476))
    log('MANUAL (needs the scope): probe PC4, press B1 once, capture the frame.')


def task_A3a(portname, log):
    port = flash_and_startup('A3a', portname, log)
    back = burst(port, log, 1)
    port.close()
    log.check(back == BURST, 'echo by polling, no delay: all 10 characters return')


def task_A3b(portname, log):
    port = flash_and_startup('A3b', portname, log)
    b1 = burst(port, log, 1)
    b2 = burst(port, log, 2)
    port.close()
    (isr,) = read32(LPUART1_ISR)
    rxne = (isr >> 5) & 1
    ore = (isr >> 3) & 1
    log('WATCH (read over ST-LINK after both bursts; RDR not read):')
    log('  LPUART1->ISR = 0x%08X' % isr)
    log('  (LPUART1->ISR >> 5) & 1U  RXNE = %d' % rxne)
    log('  (LPUART1->ISR >> 3) & 1U  ORE  = %d' % ore)
    log.check(b1 == b'0', 'burst 1: only "0" returns (the rest is lost by overrun)')
    log.check(b2 == b'', 'burst 2: nothing returns (ORE stays 1)')
    log.check(rxne == 0 and ore == 1, 'RXNE = 0 and ORE = 1')


def task_A3c(portname, log):
    port = flash_and_startup('A3c', portname, log)
    b1 = burst(port, log, 1)
    b2 = burst(port, log, 2)
    port.close()
    cr1, iser2, isr = read32(LPUART1_CR1, NVIC_ISER2, LPUART1_ISR)
    log('  LPUART1->CR1 = 0x%08X   NVIC->ISER[2] = 0x%08X   LPUART1->ISR = 0x%08X' % (cr1, iser2, isr))
    log.check(b1 == BURST and b2 == BURST, 'echo by interrupt: all 10 characters return, both times')
    log.check((cr1 >> 5) & 1, 'RXNEIE (CR1 bit 5) = 1')
    log.check((iser2 >> 27) & 1, 'NVIC ISER[2] bit 27 = 1 (IRQ 91 = 64 + 27, LPUART1)')
    log.check(((isr >> 3) & 1) == 0, 'ORE = 0 (no overrun)')


def modbus(portname, log, *args):
    cmd = [sys.executable, MODBUS, '--port', portname] + list(args)
    log('$ python lab02_modbus.py %s' % ' '.join(['--port', portname] + list(args)))
    r = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
    for line in (r.stdout + r.stderr).splitlines():
        log('  ' + line)
    log.check(r.returncode == 0, 'lab02_modbus.py exit status %d' % r.returncode)
    return r.stdout


def flash_b(task, log):
    flash(task, log)
    time.sleep(0.5)


def task_B1(portname, log):
    flash_b('B1', log)
    modbus(portname, log, '--write-only')


def task_B2(portname, log):
    flash_b('B2', log)
    modbus(portname, log)
    out = modbus(portname, log, '--exceptions')
    codes = re.findall(r'exception code (\d+)', out)
    log.check(codes == ['2', '1'], 'exception codes: register 9 -> 2, function 0x04 -> 1 (got %s)' % codes)
    reset_board(log)
    out = modbus(portname, log, '--errors', '0.3')
    bads = re.findall(r'bad=(\d+)', out)
    wrong = re.search(r'(\d+) with a wrong bit', out)
    if bads and wrong:
        log('  last read bad = %s, [PC] done: %s with a wrong bit' % (bads[-1], wrong.group(1)))


def task_B3(portname, log):
    flash_b('B3', log)
    out = modbus(portname, log)
    sw = [int(v) for v in re.findall(r'crc_sw=(\d+)', out)]
    hw = [int(v) for v in re.findall(r'crc_hw=(\d+)', out)]
    if sw and hw:
        common_sw = max(set(sw), key=sw.count)
        common_hw = max(set(hw), key=hw.count)
        log('  crc_sw: most common %d cycles (min %d, max %d)' % (common_sw, min(sw), max(sw)))
        log('  crc_hw: most common %d cycles (min %d, max %d)' % (common_hw, min(hw), max(hw)))
        log('  at 170 MHz: sw %.2f us, hw %.3f us, ratio %.2f' % (
            common_sw / 170.0, common_hw / 170.0, common_sw / common_hw))
        log.check(common_hw < common_sw, 'the CRC unit is faster than the software CRC')


FOLDER = {'A1': 'A1', 'A2': 'A2', 'A3a': 'A3', 'A3b': 'A3', 'A3c': 'A3', 'B1': 'B1', 'B2': 'B2', 'B3': 'B3'}


def default_port():
    """The ST-LINK virtual COM port (ST USB vendor 0x0483), else COM8."""
    try:
        from serial.tools import list_ports
        st = [p.device for p in list_ports.comports() if p.vid == 0x0483]
        if st:
            return st[0]
    except Exception:
        pass
    return 'COM8'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tasks', nargs='*', default=TASKS, help='tasks to check (default: all)')
    ap.add_argument('--port', default=default_port(), help='serial port of the board (default: found automatically)')
    a = ap.parse_args()
    bad_names = [t for t in a.tasks if t not in TASKS]
    if bad_names:
        ap.error('unknown task(s): %s' % ', '.join(bad_names))

    try:
        open_port(a.port).close()
    except serial.SerialException as e:
        sys.exit('Cannot open %s: %s\nClick "Stop Monitoring" in the Serial Monitor, then run again.' % (a.port, e))

    results = []
    for task in a.tasks:
        log = Log(task)
        log('=== %s  (%s, port %s)' % (task, time.strftime('%Y-%m-%d %H:%M:%S'), a.port))
        try:
            globals()['task_' + task](a.port, log)
        except Exception as e:  # keep going with the next task
            log.check(False, 'error: %s' % e)
        log.save(FOLDER[task], 'check-%s.txt' % task)
        results.append((task, log.ok))
        print()

    print('=== summary')
    for task, ok in results:
        print('  %-4s %s' % (task, 'PASS' if ok else 'FAIL'))
    return 0 if all(ok for _, ok in results) else 1


if __name__ == '__main__':
    sys.exit(main())
