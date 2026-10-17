"""Simulated hardware: I2C receivers (gates) + IR emitters (cars).

Replaces Wire.requestFrom / Wire.write and the DEBUG_FAKE_ID part of the firmware.
Used by the PC simulator, and by the ESP32 in demo mode (no gate wired).
CPython / MicroPython common subset only.
"""
import random, struct
from ricino import core


class FakeGate:
    """One I2C receiver: timestamps the IR passes with its own clock, started by 0x8A."""
    def __init__(self, address):
        self.address = address
        self.start_ms = None
        self.queue = []

    def command(self, cmd):
        if cmd[0] == 0x8A:
            self.start_ms, self.queue = core.millis(), []
        elif cmd[0] == 0x8F:
            self.start_ms = None

    def detect(self, ID):
        if self.start_ms is None:
            return
        t = core.millis() - self.start_ms
        body = struct.pack("<II", ID, t) + bytes([random.randint(20, 255), random.randint(20, 255)])
        self.queue.append(bytes([0x84, self.address, core.crc8(body)]) + body)

    def request(self):
        if self.queue:
            return self.queue.pop(0)
        state = 3 if self.start_ms is not None else 1
        body = bytes([state, random.randint(0, 3), len(self.queue), random.randint(5, 20)])
        packet = bytes([0x82, self.address, core.crc8(body)]) + body
        return packet + bytes(core.PACKET_SIZE - len(packet))


class SimBus:
    """Same interface as the real I2C bus."""
    def __init__(self):
        self.gates = {}
        for a in core.address_all_gates[: core.NUMBER_GATES_MAX]:
            self.gates[a] = FakeGate(a)

    def request(self, address, n):
        g = self.gates.get(address)
        return g.request() if g else b""

    def write(self, address, data):
        g = self.gates.get(address)
        if g:
            g.command(data)


class FakeCar:
    """An IR emitter driving laps: goes through gate 0, 1, ... with some jitter."""
    def __init__(self, bus, ID, base_lap):
        self.bus = bus
        self.ID = ID
        self.lap = base_lap * random.uniform(0.85, 1.2)                  # driver skill
        self.gate = 0
        self.next = core.millis() + int(random.uniform(0.2, 1.5) * 1000)  # grid -> finish line

    def loop(self):
        if core.millis() < self.next:
            return
        n = core.ui_config.gates
        if self.gate == 0 or random.random() > 0.02:          # 2 % missed detections on intermediate gates
            self.bus.gates[core.address_all_gates[self.gate]].detect(self.ID)
        self.gate = (self.gate + 1) % n
        sector = self.lap / n * random.uniform(0.9, 1.12)
        if random.random() < 0.03:
            sector *= random.uniform(1.5, 2.5)                             # crash!
        self.next = core.millis() + int(sector * 1000)


class Cars:
    """Puts cars on track at the green flag, removes them before the next start."""
    def __init__(self, bus, laptime=6.0):
        self.bus, self.laptime, self.cars = bus, laptime, []
        self.next_read = 0

    def read_mode(self):
        """Tag reading: a car passes the finish line every 2-3.5 s, a known emitter or a new one."""
        now = core.millis()
        if now < self.next_read:
            return
        self.next_read = now + random.randint(2000, 3500)
        known = [p["id"] for p in core.ui_config.names[: core.ui_config.players]]
        ID = random.choice(known) if known and random.random() < 0.5 else random.randint(1000, 9999)
        self.bus.gates[core.address_all_gates[0]].detect(ID)

    def loop(self):
        if core.ui_config.read_id:
            self.read_mode()
            return
        s = core.raceState
        if s == core.RACE or s == core.FINISH:
            if not self.cars:
                self.cars = [FakeCar(self.bus, core.ui_config.names[i]["id"], self.laptime)
                             for i in range(core.ui_config.players)]
            for c in self.cars:
                c.loop()
        elif self.cars and s in (core.WAIT, core.START, core.WARMUP):
            self.cars = []


def install(laptime=6.0):
    """Create the simulated bus and register the cars in the core loop; returns the bus."""
    bus = SimBus()
    core.loop_hooks.append(Cars(bus, laptime).loop)
    return bus
