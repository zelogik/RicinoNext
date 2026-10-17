"""RicinoNext RS485 bring-up: listen only (MicroPython on the ESP32). Sends nothing for 20 s and prints
every byte heard, with the time. Power-cycle the gate meanwhile: its "H21\\n" must show up intact.

  uvx mpremote connect /dev/ttyUSB0 run receiver/bringup/rs485_listen.py
"""
import time
from machine import UART

TX, RX, BAUD, SECONDS = 26, 25, 9600, 20

u = UART(2, baudrate=BAUD, tx=TX, rx=RX, timeout=0, rxbuf=512)
while u.any():
    u.read()
print("listening %d s on RX=%d at %d bauds: power-cycle the gate now" % (SECONDS, RX, BAUD))
t0 = time.ticks_ms()
total = 0
while time.ticks_diff(time.ticks_ms(), t0) < SECONDS * 1000:
    if u.any():
        time.sleep_ms(20)                  # let a whole message arrive
        data = u.read()
        total += len(data)
        ok = b"H2" in data or b"G2" in data
        print("%5.1f s  %2d bytes  %-40r %s" % (time.ticks_diff(time.ticks_ms(), t0) / 1000, len(data), data,
                                                "<- valid gate message" if ok else ""))
    time.sleep_ms(2)
print("done: %d bytes heard" % total)
