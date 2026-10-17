"""Start lights and race status on a WS2812 strip (8 LEDs by default), F1 style.

  countdown > 5 s   amber blink                    last 5 s   red bar filling, one step per second
  GO                all out, then green for 2 s    last lap   white blink
  finish            white checkerboard flashing    tag read   dim blue
  otherwise         off

frame() is a pure function of the race state (tested on the PC); Strip writes only on change.
CPython / MicroPython common subset.
"""
from ricino import core

OFF = (0, 0, 0)
RED = (255, 0, 0)
AMBER = (255, 100, 0)
GREEN = (0, 255, 0)
WHITE = (255, 255, 255)
BLUE = (0, 0, 60)
STEPS = 5               # F1: five lights, one per second
GO_MS = 2000            # green after the lights go out


def frame(n, now=None):
    """Colors of the n LEDs for the current race state."""
    now = core.millis() if now is None else now
    s, c, race = core.raceState, core.ui_config, core.race
    if c.read_id:
        return [BLUE] * n
    if s == core.WARMUP:
        left = c.warmup * 1000 - (now - race.delay_warmup_timer)
        if left > STEPS * 1000:
            return [AMBER if (left // 500) % 2 else OFF] * n
        lit = STEPS - (max(left, 1) + 999) // 1000 + 1          # 1 .. 5 lights
        on = (n * lit + STEPS - 1) // STEPS                       # spread over the strip
        return [RED] * on + [OFF] * (n - on)
    if s == core.RACE:
        if now - race.start_time < GO_MS:
            return [GREEN] * n
        if c.style == core.LAPS and c.laps_condition > 1 and race.biggest_lap == c.laps_condition - 1:
            return [WHITE if (now // 500) % 2 else OFF] * n         # last lap for the leader
        return [OFF] * n
    if s == core.FINISH:
        phase = (now // 250) % 2
        return [WHITE if (i + phase) % 2 else OFF for i in range(n)]
    return [OFF] * n


class Strip:
    """WS2812 on one GPIO (MicroPython neopixel), refreshed at most every 40 ms."""
    def __init__(self, pin, count=8, brightness=0.4):
        import machine, neopixel
        self.np = neopixel.NeoPixel(machine.Pin(pin), count)
        self.n, self.level = count, max(0.0, min(1.0, brightness))
        self.last, self.next_ms = None, 0
        self.show([OFF] * count)

    def show(self, colors):
        for i, (r, g, b) in enumerate(colors):
            self.np[i] = (int(r * self.level), int(g * self.level), int(b * self.level))
        self.np.write()

    def loop(self):
        now = core.millis()
        if now < self.next_ms:
            return
        self.next_ms = now + 40
        f = frame(self.n, now)
        if f != self.last:
            self.last = f
            self.show(f)
