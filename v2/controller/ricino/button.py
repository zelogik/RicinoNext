"""Physical Start/Stop control (ESP32 GPIO to GND, internal pull-up).

  mode "button" (push button): short press = start (when idle); hold 1 s = stop (during a race,
                               so a knock does not end it)
  mode "switch" (toggle switch): ON = start, OFF = stop. Only changes count: a switch left ON at
                               boot does not start a race.

Debounced (30 ms), sampled every 10 ms from the core loop. CPython / MicroPython common subset;
read() is injected (tests use a fake input).
"""
from ricino import core

DEBOUNCE_MS = 30
HOLD_MS = 1000
SAMPLE_MS = 10


def racing():
    return core.START <= core.raceState <= core.FINISH


class Button:
    def __init__(self, read, mode="button"):
        self.read, self.mode = read, mode
        self.raw = self.stable = bool(read())
        self.since = self.pressed_at = self.last_sample = 0
        self.used = True                 # current press already acted (start) or nothing to do

    def loop(self, now=None):
        now = core.millis() if now is None else now
        if now - self.last_sample < SAMPLE_MS:
            return
        self.last_sample = now
        raw = bool(self.read())
        if raw != self.raw:
            self.raw, self.since = raw, now
        if self.raw != self.stable and now - self.since >= DEBOUNCE_MS:
            self.stable = self.raw
            if self.mode == "switch":
                core.request_state(self.stable)
                return
            if self.stable:                                   # pressed
                self.pressed_at, self.used = now, False
                if not racing():
                    core.request_state(True)
                    self.used = True
        if self.mode == "button" and self.stable and not self.used and racing() \
                and now - self.pressed_at >= HOLD_MS:
            self.used = True
            core.request_state(False)
