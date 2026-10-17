"""RicinoNext RS485 bring-up test, controller side (MicroPython on the ESP32).

Polls the gates 21..28 with "T21\\n" ... every 0.5 s (their LED toggles) and prints what comes back.
The application is stopped while it runs; press the reset button (or mpremote reset) afterwards.

  uvx mpremote connect /dev/ttyUSB0 run receiver/bringup/rs485_bringup_controller.py
"""
import time
from machine import UART

TX, RX, BAUD = 26, 25, 9600     # ESP32 pins to the RS485 module, as in the system page
ADDRESSES = range(21, 29)
ROUNDS = 20                     # about 20 x 8 x 0.06 s... then a summary

u = UART(2, baudrate=BAUD, tx=TX, rx=RX, timeout=0, rxbuf=512)
stats = {a: 0 for a in ADDRESSES}
echo = other = 0


def read_for(ms):
    buf = b""
    t0 = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), t0) < ms:
        if u.any():
            buf += u.read()
        time.sleep_ms(2)
    return buf


print("RS485 bring-up: TX=%d RX=%d %d bauds, gates %d..%d" % (TX, RX, BAUD, ADDRESSES[0], ADDRESSES[-1]))
start = read_for(300)
if start:
    print("heard before polling:", start)
for r in range(ROUNDS):
    seen = []
    for a in ADDRESSES:
        msg = ("T%02d\n" % a).encode()
        u.write(msg)
        got = read_for(60)                  # 4 bytes out + 4 back at 9600 ~ 10 ms
        if got.startswith(msg):            # transceiver hearing its own request
            echo += 1
            got = got[len(msg):]
        answer = ("G%02d\n" % a).encode()
        if answer in got:
            stats[a] += 1
            seen.append(a)
            got = got.replace(answer, b"")
        if got:
            other += len(got)
            print("  after T%02d, unexpected bytes: %r" % (a, got))
    print("round %2d: answers from %s" % (r + 1, seen or "nobody"))
    time.sleep_ms(500 - 60 * len(ADDRESSES) if 60 * len(ADDRESSES) < 500 else 0)

print("summary: answers per gate", {a: n for a, n in stats.items() if n} or "none",
      "| echoes of our requests:", echo, "| unexpected bytes:", other)
