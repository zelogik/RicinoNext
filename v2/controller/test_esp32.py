#!/usr/bin/env python3
"""Runs ricino/esp32.py on CPython with fake MicroPython modules (machine, network, esp32, ticks).

Checks the ESP32-only code paths without a board: setup access point, system API,
Wi-Fi/hostname config, admin password, file OTA (gz shadowing), firmware OTA, captive portal.
It cannot prove MicroPython compatibility (run on a board for that), only the logic.
"""
import asyncio, gc, json, os, shutil, sys, tempfile, time, types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# ---- fake MicroPython runtime -----------------------------------------------------------
_t0 = time.monotonic()
time.ticks_ms = lambda: int((time.monotonic() - _t0) * 1000)
time.ticks_us = lambda: int((time.monotonic() - _t0) * 1e6)
time.ticks_diff = lambda a, b: a - b
time.ticks_add = lambda a, b: a + b
time.sleep_ms = lambda ms: time.sleep(ms / 1000)
time.sleep_us = lambda us: time.sleep(us / 1e6)
asyncio.sleep_ms = lambda ms: asyncio.sleep(ms / 1000)
gc.mem_free = lambda: 100000
gc.mem_alloc = lambda: 20000

machine = types.ModuleType("machine")
machine.resets = 0
def _reset():
    machine.resets += 1
machine.reset = _reset
machine.Pin = lambda n: n
machine.freq = lambda: 240000000
class I2C:
    def __init__(self, *a, **k): pass
    def readfrom(self, a, n): raise OSError(19)
    def writeto(self, a, d): pass
    def scan(self): return [21, 22]
machine.I2C = I2C
machine.SoftI2C = I2C

network = types.ModuleType("network")
network.STA_IF, network.AP_IF, network.AUTH_OPEN, network.AUTH_WPA_WPA2_PSK = 0, 1, 0, 4
network.hostname = lambda h: None
class WLAN:
    def __init__(self, i): self.i, self.cfg = i, {}
    def active(self, *a): return True
    def config(self, *a, **k):
        if a == ("mac",):
            return b"\x24\x0a\xc4\x12\xab\xcd"
        self.cfg.update(k)
    def connect(self, *a): pass
    def isconnected(self): return False
    def disconnect(self): pass
    def ifconfig(self): return ("192.168.4.1", "255.255.255.0", "192.168.4.1", "192.168.4.1")
    def status(self, k): return -60
    def scan(self): return [(b"Club", b"", 1, -50, 3, 0), (b"Club", b"", 6, -70, 3, 0), (b"Open", b"", 1, -80, 0, 0)]
network.WLAN = WLAN

esp32mod = types.ModuleType("esp32")
class Partition:
    RUNNING = 0
    booted = None
    def __init__(self, kind=0, label="ota_0"): self.label, self.blocks = label, {}
    def get_next_update(self): return OTA
    def info(self): return (0, 16, 0x1d0000, 1536 * 1024, self.label, False)
    def writeblocks(self, n, buf):
        assert len(buf) == 4096
        self.blocks[n] = bytes(buf)
    def set_boot(self): Partition.booted = self.label
    @staticmethod
    def mark_app_valid_cancel_rollback(): pass
OTA = Partition(label="ota_1")
esp32mod.Partition = Partition
neopixel = types.ModuleType("neopixel")
class NeoPixel(list):
    writes = 0
    def __init__(self, pin, n): super().__init__([(0, 0, 0)] * n)
    def write(self): NeoPixel.writes += 1
neopixel.NeoPixel = NeoPixel
sys.modules.update(machine=machine, network=network, esp32=esp32mod, neopixel=neopixel)

from ricino import core, esp32, lights, rs485, sim, web   # noqa: E402


class FakeUART:
    """RS485 line with two gates (21, 22) answering like the receiver firmware should."""
    def __init__(self, *a, **k):
        self.rx = b""
        self.gates = {21: sim.FakeGate(21), 22: sim.FakeGate(22)}
        self.last = {}              # address -> last reply (for REPEAT)
        self.corrupt_next = False   # simulate a reply damaged on the cable

    def any(self):
        return len(self.rx)

    def read(self, n=None):
        d, self.rx = self.rx, b""
        return d

    def write(self, data):
        req = rs485.parse_request(data)
        if not req:
            return
        addr, cmd = req
        if addr == rs485.BROADCAST:
            targets = list(self.gates.values())
        else:
            targets = [self.gates[addr]] if addr in self.gates else []
        for g in targets:
            if cmd in (rs485.POLL, rs485.REPEAT) and addr != rs485.BROADCAST:
                if cmd == rs485.POLL or addr not in self.last:
                    self.last[addr] = rs485.reply(addr, g.request())
                frame = bytearray(self.last[addr])
                if self.corrupt_next:
                    self.corrupt_next = False
                    frame[8] ^= 0xFF
                self.rx += b"\x00\xa5\x13" + bytes(frame)                   # line noise before the reply
            elif cmd in (0x8A, 0x8F):
                g.command(bytes([cmd]))


machine.UART = FakeUART

ROOT = tempfile.mkdtemp(prefix="ricino-esp32-")
esp32.ROOT = ROOT
os.makedirs(ROOT + "/data")
shutil.copy(os.path.join(HERE, "data", "system.html"), ROOT + "/data/system.html")
PORT = 18110


async def http(method, path, body=b"", headers=None):
    r, w = await asyncio.open_connection("127.0.0.1", PORT)
    h = "".join("%s: %s\r\n" % kv for kv in (headers or {}).items())
    w.write(("%s %s HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\n%s\r\n" % (method, path, len(body), h)).encode() + body)
    await w.drain()
    data = await r.read()
    w.close()
    head, _, payload = data.partition(b"\r\n\r\n")
    status = int(head.split()[1])
    try:
        return status, json.loads(payload), head
    except ValueError:
        return status, payload, head


async def main():
    cfg = esp32.load_config()
    esp32.wifi_start(cfg)
    assert esp32.net["mode"] == "ap" and esp32.net["ssid"] == "RicinoNext-ABCD", esp32.net
    bus = esp32.I2CBus(cfg["i2c"])
    assert bus.request(21, 13) == b"", "missing gate must read as empty"
    clock = esp32.Clock()
    server = web.Server(ROOT + esp32.DATA_DIR, esp32.make_routes(cfg, bus), esp32.captive)
    core.setup(bus, clock.millis, clock.elapsed_us, lambda: "1", server.broadcast, server.send_to)
    await server.start(PORT, "127.0.0.1")
    loop_task = asyncio.create_task(web.run_loop(0.002))
    esp32.reboot_later = lambda ms=0: asyncio.sleep(0)       # no real reboot in tests

    s, d, _ = await http("GET", "/api/system")
    assert s == 200 and d["platform"] == "esp32" and d["ota"] and d["gates"] == [21, 22] and d["mode"] == "ap", d
    assert d["stats"]["mhz"] == 240 and d["stats"]["ram_free"] == 100000 and "cpu" in d["stats"], d["stats"]
    print("system      : OK", d["ssid"], d["fs_free"] // 1024, "KB free")

    s, d, _ = await http("GET", "/api/scan")
    assert [n["ssid"] for n in d] == ["Club", "Open"] and d[0]["rssi"] == -50 and not d[1]["secure"], d
    print("scan        : OK")

    s, d, _ = await http("POST", "/api/config", json.dumps({"hostname": "Bad Name!"}).encode())
    assert s == 400, (s, d)
    s, d, _ = await http("POST", "/api/config", json.dumps({"hostname": "circuit-1", "ssid": "Club", "password": "secret123"}).encode())
    assert s == 200 and d["reboot"], d
    saved = json.load(open(ROOT + "/config.json"))
    assert saved["hostname"] == "circuit-1" and saved["ssid"] == "Club" and saved["i2c"]["sda"] == 21, saved
    print("config      : OK")

    # file OTA: html.gz replaces a plain html, forbidden paths refused
    open(ROOT + "/data/index.html", "w").write("old")
    s, d, _ = await http("POST", "/api/upload?path=%2Fdata%2Findex.html.gz", b"\x1f\x8b" + b"x" * 3000)
    assert s == 200 and os.path.getsize(ROOT + "/data/index.html.gz") == 3002, d
    assert not os.path.exists(ROOT + "/data/index.html"), "stale plain file must be removed"
    s, d, _ = await http("POST", "/api/upload?path=/ricino/core.py", b"# new core\n")
    assert s == 200 and open(ROOT + "/ricino/core.py").read() == "# new core\n"
    for bad in ("/config.json", "/data/../config.json", "/etc/passwd", "/data/"):
        s, d, _ = await http("POST", "/api/upload?path=" + bad, b"x")
        assert s == 400, (bad, s, d)
    print("upload      : OK")

    # firmware OTA: full USB image refused, app image written block by block
    s, d, _ = await http("POST", "/api/firmware", b"\xe9" + b"\0" * 5000)
    assert s == 500 and ".app-bin" in d["error"], d
    app = bytearray(10000)
    app[0], app[32:36] = 0xE9, b"\x32\x54\xcd\xab"
    s, d, _ = await http("POST", "/api/firmware", bytes(app))
    assert s == 200 and sorted(OTA.blocks) == [0, 1, 2] and Partition.booted == "ota_1", (d, sorted(OTA.blocks))
    assert OTA.blocks[2][10000 - 8192:] == b"\xff" * (3 * 4096 - 10000), "last block padded with 0xFF"
    print("firmware    : OK")

    # admin password
    cfg["admin_password"] = "pit"
    s, d, _ = await http("POST", "/api/reboot")
    assert s == 401, s
    s, d, _ = await http("POST", "/api/reboot", headers={"X-Password": "pit"})
    assert s == 200, s
    print("password    : OK")

    # /api/bus (here the I2C bus: no gate answers) and its test
    s, d, _ = await http("GET", "/api/bus")
    assert s == 200 and d["bus"] == "i2c", d
    s, d, _ = await http("GET", "/api/bus?test=1")
    assert s == 200 and len(d["test"]) == 8 and all(r["ok"] == 0 for r in d["test"].values()), d
    print("bus diag    : OK")

    # captive portal (AP mode) + static page
    s, d, head = await http("GET", "/generate_204")
    assert s == 302 and b"Location: http://192.168.4.1/system.html" in head, head
    s, d, _ = await http("GET", "/system.html")
    assert s == 200 and b"RicinoNext" in d
    print("captive/www : OK")

    # RS485 bus: scan, broadcast start, ID packet, missing gate, corrupted frames
    rb = esp32.RS485Bus(cfg["rs485"])
    assert rb.scan() == [21, 22], rb.scan()
    rb.write(rs485.BROADCAST, core.START_CMD)
    assert all(g.start_ms is not None for g in rb.uart.gates.values()), "broadcast start reaches every gate"
    rb.uart.gates[22].detect(4242)
    pkt = rb.request(22, core.PACKET_SIZE)
    assert pkt[0] == 0x84 and pkt[1] == 22 and int.from_bytes(pkt[3:7], "little") == 4242, pkt
    # reply damaged on the cable: the next poll asks for a repeat, the pass is recovered once
    rb.uart.gates[22].detect(1337)
    rb.uart.corrupt_next = True
    assert rb.request(22, core.PACKET_SIZE) == b"" and rb.retry[22], "corrupted reply refused, retry armed"
    pkt = rb.request(22, core.PACKET_SIZE)
    assert pkt[0] == 0x84 and int.from_bytes(pkt[3:7], "little") == 1337 and not rb.retry[22], pkt
    assert rb.request(22, core.PACKET_SIZE)[0] != 0x84, "not delivered twice"
    t0 = time.monotonic()
    assert rb.request(23, core.PACKET_SIZE) == b"" and time.monotonic() - t0 < 0.05, "missing gate: timeout"
    for _ in range(5):                                         # silent 5 times -> polled every 500 ms only
        rb.request(24, core.PACKET_SIZE)
    polls = rb.stats.gates[24][0]
    t0 = time.monotonic()
    assert rb.request(24, core.PACKET_SIZE) == b"" and time.monotonic() - t0 < 0.002 and rb.stats.gates[24][0] == polls, "backoff"
    bad = bytearray(rs485.reply(21, b"\x84" + bytes(12)))
    bad[-1] ^= 0xFF
    assert rs485.parse_reply(bytes(bad), 21) is None or rs485.parse_reply(bytes(bad), 21) == b"", "bad crc refused"
    assert rs485.parse_reply(rs485.reply(22, bytes(13)), 21) in (b"", None), "reply from another gate refused"
    # diagnostics: counters, a gate "hello" heard between polls, probe
    st = rb.stats.json()
    assert st["gates"]["22"]["ok"] >= 2 and st["gates"]["23"]["timeouts"] >= 1 and st["rx_bytes"] > 0, st
    echo_only = esp32.RS485Bus(cfg["rs485"])                                    # transceiver echoing our request
    echo_only.uart.write = lambda data: setattr(echo_only.uart, "rx", echo_only.uart.rx + data)
    assert echo_only.request(21, core.PACKET_SIZE) == b"" and echo_only.stats.json()["other_bytes"] == 0
    assert echo_only.stats.gates[21][esp32.BusStats.TIMEOUT] == 1, "an echo alone is no answer"
    rb.uart.rx += b"\x00" + rs485.reply(21, bytes([0x82, 21]) + bytes(11))       # gate 21 powered up
    rb.request(22, core.PACKET_SIZE)
    assert "21" in rb.stats.json()["heard"], rb.stats.json()
    pkt, raw = rb.probe(21)
    assert pkt and pkt[1] == 21 and len(raw) >= rs485.REPLY_SIZE, raw
    assert rb.probe(23) == (None, b"")
    # listen: a gate hello and a line held low by the gate module, nothing sent by us
    rb.uart.rx = b""
    async def talk():
        await asyncio.sleep(0.05)
        rb.uart.rx += rs485.reply(22, bytes([0x82, 22]) + bytes(11)) + bytes(30)
    asyncio.create_task(talk())
    heard_before = dict(rb.stats.heard)
    res = await rb.listen(300)
    assert res["bytes"] == rs485.REPLY_SIZE + 30 and res["zeros"] >= 30 and 22 in rb.stats.heard, res
    saved_bus = core._bus
    core._bus = rb
    core.command_all(core.STOP_CMD)
    assert all(g.start_ms is None for g in rb.uart.gates.values()), "command_all uses the broadcast"
    core._bus = saved_bus
    s, d, _ = await http("POST", "/api/config", json.dumps({"bus": "rs485", "rs485": {"tx": 17, "rx": 16}}).encode(),
                         headers={"X-Password": "pit"})
    saved = json.load(open(ROOT + "/config.json"))
    assert s == 200 and saved["bus"] == "rs485" and saved["rs485"]["tx"] == 17 and saved["rs485"]["baud"] == 57600, saved
    print("rs485       : OK")

    # start lights: F1 sequence on 8 LEDs (pure frames), strip writes only on change
    R, O, A, G, W = lights.RED, lights.OFF, lights.AMBER, lights.GREEN, lights.WHITE
    saved_state = core.raceState
    core.ui_config.warmup, core.race.delay_warmup_timer, core.raceState = 10, 0, core.WARMUP
    assert set(lights.frame(8, 1000)) <= {A, O} and lights.frame(8, 1000) != lights.frame(8, 1500), "amber blink"
    bars = [lights.frame(8, t).count(R) for t in (5500, 6500, 7500, 8500, 9500)]
    assert bars == [2, 4, 5, 7, 8], bars
    core.raceState, core.race.start_time = core.RACE, 10000
    assert lights.frame(8, 10500) == [G] * 8 and lights.frame(8, 13000) == [O] * 8, "green then off"
    core.ui_config.style, core.ui_config.laps_condition, core.race.biggest_lap = core.LAPS, 5, 4
    assert W in lights.frame(8, 20500) or W in lights.frame(8, 21000), "last lap: white blink"
    core.raceState = core.FINISH
    f = lights.frame(8, 30000)
    assert f.count(W) == 4 and f != lights.frame(8, 30250), "checkerboard"
    core.raceState, core.race.biggest_lap = core.WAIT, 0
    assert lights.frame(8) == [O] * 8
    strip = lights.Strip(27, 8, 0.5)
    w0 = NeoPixel.writes
    core.raceState = core.WARMUP
    core.race.delay_warmup_timer = core.millis() - 9200          # 0.8 s left: 5 lights
    strip.loop()
    assert strip.np[7] == (127, 0, 0) and NeoPixel.writes == w0 + 1, strip.np
    strip.next_ms = 0
    strip.loop()
    assert NeoPixel.writes == w0 + 1, "same frame: no write"
    core.raceState = saved_state
    print("lights      : OK", bars)

    # start/stop push button: debounce, short press = start, knock = nothing, hold 1 s = stop
    from ricino import button
    level = [False]
    sent = []
    real_conf = core.conf_to_json
    core.conf_to_json = lambda client=None: sent.append(core.raceState)
    core.raceState = core.WAIT
    b = button.Button(lambda: level[0], "button")
    t = 100000

    def run(ms, pressed):
        nonlocal t
        level[0] = pressed
        for _ in range(ms // 10):
            t += 10
            b.loop(t)

    run(20, True); run(20, False)                  # 20 ms bounce: ignored
    assert core.raceState == core.WAIT, "bounce must not start"
    run(100, True); run(100, False)                # short press
    assert core.raceState == core.START, core.raceState
    core.raceState = core.RACE
    run(300, True); run(100, False)                # knock during the race
    assert core.raceState == core.RACE, "short press must not stop a race"
    run(1200, True)                                # hold 1 s
    assert core.raceState == core.STOP, core.raceState
    run(100, False)
    # toggle switch: left ON at boot does nothing, OFF->ON starts, ON->OFF stops
    core.raceState = core.WAIT
    level[0] = True
    sw = button.Button(lambda: level[0], "switch")
    for _ in range(10):
        t += 10
        sw.loop(t)
    assert core.raceState == core.WAIT, "switch ON at boot must not start"
    for v, expect in ((False, core.WAIT), (True, core.START)):
        level[0] = v
        for _ in range(10):
            t += 10
            sw.loop(t)
        assert core.raceState == expect, (v, core.raceState)
    core.raceState = core.RACE
    level[0] = False
    for _ in range(10):
        t += 10
        sw.loop(t)
    assert core.raceState == core.STOP and len(sent) == 4, (core.raceState, sent)
    core.conf_to_json = real_conf
    core.raceState = saved_state
    print("button      : OK")

    loop_task.cancel()
    print("OK")


try:
    asyncio.run(main())
finally:
    shutil.rmtree(ROOT, ignore_errors=True)
