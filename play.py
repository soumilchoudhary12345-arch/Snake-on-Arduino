#!/usr/bin/env python3
"""Play the Arduino Snake game with your keyboard's arrow keys.

    python3 play.py [port]        default port: /dev/ttyUSB0

Connects to the board at 115200 baud, forwards the arrow keys (the same
ESC [ A/B/C/D sequences the Serial Monitor sends) and echoes everything
the sketch prints back to your terminal.  Quit with Ctrl-C.
"""
import os
import select
import sys
import termios
import tty

import serial

PORT = '/dev/ttyUSB0'
BAUD = 115200
LEAVE = (b'\x03', b'\x04')  # Ctrl-C / Ctrl-D when the terminal is in raw mode


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    ser = serial.Serial(port, BAUD, timeout=0)

    interactive = sys.stdin.isatty()
    if interactive:
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        tty.setraw(fd)
        print('connected to %s - arrow keys play, Ctrl-C quits\r' % port)
    else:
        fd = sys.stdin.fileno()

    try:
        while True:
            readable, _, _ = select.select([ser, fd], [], [], 0.05)

            if ser in readable:
                data = ser.read(ser.in_waiting or 1)
                sys.stdout.write(data.decode('ascii', 'replace'))
                sys.stdout.flush()

            if fd in readable:
                keys = os.read(fd, 64)
                if not keys:
                    break
                if keys in LEAVE:
                    break
                ser.write(keys)   # pass the arrow sequence straight through
                ser.flush()
    except KeyboardInterrupt:
        pass
    finally:
        if interactive:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
        ser.close()
        print('\nbye')


if __name__ == '__main__':
    main()
