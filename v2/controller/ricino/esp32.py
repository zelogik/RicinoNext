"""ESP32 platform (MicroPython): real I2C gates, Wi-Fi, hostname, system page API, OTA.

Wi-Fi: joins the configured network (station). Without one, or if it fails within 15 s,
opens a setup access point "RicinoNext-XXXX" (192.168.4.1) with a captive DNS so phones
open the system page by themselves. The hostname is also the mDNS name (<hostname>.local).

System API (used by data/system.html; POST routes need X-Password if admin_password is set):
  GET  /api/system            device info
  GET  /api/scan              visible Wi-Fi networks
  POST /api/config            JSON: ssid, password, hostname, ap_password, admin_password, simulate, i2c
  POST /api/upload?path=/x    raw body -> file (written to x.new, then renamed)
  POST /api/firmware          raw body -> next OTA partition (MicroPython "OTA" firmware build)
  POST /api/reboot

Start lights: WS2812 strip on GPIO 27 (ricino/lights.py), optional.
Start/Stop button or switch: GPIO 33 to GND (ricino/button.py), optional.
Gates: I2C (SDA 21 / SCL 22) or RS485 (UART2 TX 26 / RX 25, 115200, ricino/rs485.py).
Settings live in /config.json (device) and /race.json (race settings, drivers).
"""
import asyncio, gc, json, machine, network, os, socket, time
from ricino import core, rs485, web

ROOT = ""                  # filesystem root ("" on the board; a temp dir in tests)
CONFIG = "/config.json"
SETTINGS = "/race.json"    # race settings + drivers (the firmware's EEPROM todo)
RECORDS = "/records.json"  # arcade top 10
DATA_DIR = "/data"
DEFAULTS = {"ssid": "", "password": "", "hostname": "ricinonext", "ap_password": "", "admin_password": "",
            "simulate": False, "port": 80, "bus": "i2c",
            "i2c": {"id": 0, "sda": 21, "scl": 22, "freq": 100000, "soft": False},
            "rs485": {"uart": 2, "tx": 26, "rx": 25, "baud": 57600, "timeout": 10},
            "leds": {"enabled": False, "pin": 27, "count": 8, "brightness": 0.4},
            "button": {"enabled": False, "pin": 33, "mode": "button"}}
UPLOAD_DIRS = ("/data/", "/ricino/")
UPLOAD_FILES = ("/main.py", "/boot.py")

net = {"mode": "", "ip": "", "ssid": "", "mac": ""}
_boot_ms = time.ticks_ms()


# ----------------------------------------------------------------------------
#  Config
# ----------------------------------------------------------------------------
def load_config():
    cfg = json.loads(json.dumps(DEFAULTS))          # deep copy
    try:
        with open(ROOT + CONFIG) as f:
            saved = json.load(f)
        for k in saved:
            if isinstance(cfg.get(k), dict) and isinstance(saved[k], dict):
                cfg[k].update(saved[k])
            else:
                cfg[k] = saved[k]
    except (OSError, ValueError):
        pass
    return cfg


def save_config(cfg):
    path = ROOT + CONFIG
    with open(path + ".new", "w") as f:
        json.dump(cfg, f)
    remove(path)
    os.rename(path + ".new", path)


# ----------------------------------------------------------------------------
#  Clocks (ticks wrap after ~12 days: accumulate)
# ----------------------------------------------------------------------------
class Clock:
    def __init__(self):
        self.last, self.total = time.ticks_ms(), 0
        self.last_us = time.ticks_us()

    def millis(self):
        now = time.ticks_ms()
        self.total += time.ticks_diff(now, self.last)
        self.last = now
        return self.total

    def elapsed_us(self):
        now = time.ticks_us()
        dt = time.ticks_diff(now, self.last_us)
        self.last_us = now
        return dt


# ----------------------------------------------------------------------------
#  Real I2C bus (Wire)
# ----------------------------------------------------------------------------
class BusStats:
    """Link counters per gate, for the system page (/api/bus): is anything coming back at all?"""
    POLLS, OK, BAD, TIMEOUT = range(4)

    def __init__(self):
        self.gates = {}             # address -> [polls, valid replies, bad replies, timeouts]
        self.rx_bytes = 0           # every byte received on the bus (RS485)
        self.heard = {}             # address -> ticks_ms of an unasked "hello" (gate power-up)
        self.raw = {}               # address -> last bytes received that were not a valid reply
        self.other = 0              # bytes received that were NOT the echo of our own requests

    def count(self, address, kind):
        g = self.gates.get(address)
        if g is None:
            g = self.gates[address] = [0, 0, 0, 0]
        g[kind] += 1

    def json(self):
        now = time.ticks_ms()
        gates = {}
        for a in sorted(self.gates):
            p, ok, bad, to = self.gates[a]
            gates[str(a)] = dict(polls=p, ok=ok, bad=bad, timeouts=to, raw=self.raw.get(a, ""))
        heard = {}
        for a in self.heard:
            heard[str(a)] = time.ticks_diff(now, self.heard[a]) // 1000
        return dict(rx_bytes=self.rx_bytes, other_bytes=self.other, gates=gates, heard=heard)


class I2CBus:
    def __init__(self, c):
        self.stats = BusStats()
        sda, scl = machine.Pin(c["sda"]), machine.Pin(c["scl"])
        if c.get("soft"):           # bit-banged: tolerates the AVR slaves' clock stretching better
            self.i2c = machine.SoftI2C(sda=sda, scl=scl, freq=c["freq"])
        else:
            self.i2c = machine.I2C(c["id"], sda=sda, scl=scl, freq=c["freq"])

    def request(self, address, n):
        self.stats.count(address, BusStats.POLLS)
        try:
            data = self.i2c.readfrom(address, n)
        except OSError:            # no gate at this address / bus error
            self.stats.count(address, BusStats.TIMEOUT)
            return b""
        self.stats.count(address, BusStats.OK if data and data[0] in (0x82, 0x83, 0x84) else BusStats.BAD)
        return data

    def probe(self, address):
        """Diagnostic: (packet or None, raw bytes)."""
        try:
            data = self.i2c.readfrom(address, core.PACKET_SIZE)
            return (data if data[0] in (0x82, 0x83, 0x84) else None), data
        except OSError:
            return None, b""

    def write(self, address, data):
        try:
            self.i2c.writeto(address, data)
        except OSError:
            pass

    def scan(self):
        try:
            return self.i2c.scan()
        except OSError:
            return []


# ----------------------------------------------------------------------------
#  RS485 bus (UART + auto-direction transceiver such as HW-519, see ricino/rs485.py)
# ----------------------------------------------------------------------------
class RS485Bus:
    broadcast = True                # address 0: one start frame for every gate

    def __init__(self, c):
        self.uart = machine.UART(c["uart"], baudrate=c["baud"], tx=c["tx"], rx=c["rx"], timeout=0, rxbuf=512)
        self.timeout = c["timeout"]
        self.stats = BusStats()
        self.retry = {}             # address -> True: last reply lost, ask the gate to send it again
        self.silent = {}            # address -> consecutive polls without any answer
        self.next_try = {}          # address -> ticks_ms of the next poll of a silent gate
        self.last = {}              # address -> last packet accepted (drops a repeated duplicate)

    def _read(self):
        data = self.uart.read() or b""
        self.stats.rx_bytes += len(data)
        return data

    def _hellos(self, data):
        """Note every valid reply frame in data, whatever its address (a gate "hello" at power-up)."""
        i = data.find(b"\x5a")
        while 0 <= i < len(data) - 1:
            if rs485.parse_reply(data[i:], data[i + 1]):
                self.stats.heard[data[i + 1]] = time.ticks_ms()
            i = data.find(b"\x5a", i + 1)

    def _flush(self):
        """Drop what arrived between two polls, but note any gate "hello"."""
        data = b""
        while self.uart.any():
            data += self._read()
        if data:
            self.stats.other += len(data)
            self._hellos(data)

    def _strip_echo(self, buf, frame):
        """Auto-direction transceivers hear their own request: count only what came from elsewhere."""
        if buf.startswith(frame):
            buf = buf[len(frame):]
        return buf

    def request(self, address, n):
        """Poll one gate; b"" if it does not answer within timeout ms. A gate silent for 5 polls in a
        row is polled only every 500 ms (an absent gate would block the loop timeout ms every time)."""
        if self.silent.get(address, 0) >= 5 and time.ticks_diff(self.next_try.get(address, 0), time.ticks_ms()) > 0:
            return b""
        self._flush()
        self.stats.count(address, BusStats.POLLS)
        repeat = self.retry.get(address, False)
        self.uart.write(rs485.request(address, rs485.REPEAT if repeat else rs485.POLL))
        buf = b""
        t0 = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), t0) < self.timeout:
            if self.uart.any():
                buf += self._read()
                packet = rs485.parse_reply(buf, address)
                if packet:
                    self.silent[address] = 0
                    self.stats.count(address, BusStats.OK)
                    self.retry[address] = False
                    if repeat and packet == self.last.get(address):
                        return b""      # we had it already (only the poll was lost)
                    self.last[address] = packet
                    return packet
                if packet is None:      # garbage only
                    break
            else:
                time.sleep_us(200)
        rest = self._strip_echo(buf, rs485.request(address, rs485.REPEAT if repeat else rs485.POLL))
        if rest:
            self.stats.other += len(rest)
            self._hellos(rest)
            self.stats.count(address, BusStats.BAD)
            self.stats.raw[address] = "".join("%02x" % b for b in rest[:40])
        else:
            self.stats.count(address, BusStats.TIMEOUT)
            self.silent[address] = self.silent.get(address, 0) + 1
            self.next_try[address] = time.ticks_add(time.ticks_ms(), 500)
        self.retry[address] = address in self.last    # known gate: its reply may hold a pass, ask again
        return b""

    def probe(self, address):
        """Diagnostic: one plain poll, listen 40 ms. (packet or None, raw bytes)."""
        self._flush()
        self.uart.write(rs485.request(address, rs485.POLL))
        buf = b""
        t0 = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), t0) < 40:
            if self.uart.any():
                buf += self._read()
            time.sleep_ms(1)
        rest = self._strip_echo(buf, rs485.request(address, rs485.POLL))
        self.stats.other += len(rest)
        self._hellos(rest)
        return rs485.parse_reply(rest, address) or None, rest

    def write(self, address, data):
        self.uart.write(rs485.request(address, data[0]))

    async def listen(self, ms):
        """Diagnostic: polling paused by the caller; record everything heard for ms (no echo possible)."""
        self._flush()
        total, zeros, sample = 0, 0, b""
        t0 = time.ticks_ms()
        while time.ticks_diff(time.ticks_ms(), t0) < ms:
            await asyncio.sleep_ms(20)
            while self.uart.any():
                data = self._read()
                total += len(data)
                zeros += data.count(b"\x00")
                if len(sample) < 48:
                    sample += data[:48 - len(sample)]
                self.stats.other += len(data)
                self._hellos(data)
        return dict(bytes=total, zeros=zeros, sample="".join("%02x" % b for b in sample))

    def scan(self):
        return [a for a in core.address_all_gates if self.request(a, core.PACKET_SIZE)]


# ----------------------------------------------------------------------------
#  Wi-Fi
# ----------------------------------------------------------------------------
def wifi_start(cfg):
    hostname = cfg["hostname"] or "ricinonext"
    try:
        network.hostname(hostname)              # MicroPython >= 1.20, also the mDNS name
    except (AttributeError, ValueError, OSError):
        pass
    sta = network.WLAN(network.STA_IF)
    sta.active(True)
    mac = sta.config("mac")
    net["mac"] = ":".join("%02x" % b for b in mac)
    if cfg["ssid"]:
        try:
            sta.config(dhcp_hostname=hostname)  # older firmware
        except (ValueError, OSError):
            pass
        print("Wi-Fi: connecting to", cfg["ssid"])
        sta.connect(cfg["ssid"], cfg["password"])
        t0 = time.ticks_ms()
        while not sta.isconnected() and time.ticks_diff(time.ticks_ms(), t0) < 15000:
            time.sleep_ms(200)
        if sta.isconnected():
            network.WLAN(network.AP_IF).active(False)
            net.update(mode="sta", ip=sta.ifconfig()[0], ssid=cfg["ssid"])
            print("Wi-Fi: connected, http://%s/ or http://%s.local/" % (net["ip"], hostname))
            return
        print("Wi-Fi: connection failed, starting the setup access point")
        sta.disconnect()                        # keep STA active: needed to scan networks
    ap = network.WLAN(network.AP_IF)
    ap.active(True)
    essid = "RicinoNext-%02X%02X" % (mac[-2], mac[-1])
    pw = cfg["ap_password"]
    try:
        if len(pw) >= 8:
            ap.config(essid=essid, authmode=network.AUTH_WPA_WPA2_PSK, password=pw)
        else:
            ap.config(essid=essid, authmode=network.AUTH_OPEN)
    except (ValueError, OSError):
        ap.config(essid=essid)
    net.update(mode="ap", ip=ap.ifconfig()[0], ssid=essid)
    print("Wi-Fi: access point %s, http://%s/" % (essid, net["ip"]))


def rssi():
    try:
        return network.WLAN(network.STA_IF).status("rssi") if net["mode"] == "sta" else None
    except (ValueError, OSError):
        return None


async def dns_server(ip):
    """Captive DNS (AP mode): every name resolves to us, so phones pop up the setup page."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", 53))
    s.setblocking(False)
    answer_ip = bytes([int(x) for x in ip.split(".")])
    while True:
        try:
            q, addr = s.recvfrom(512)
        except OSError:
            await asyncio.sleep_ms(50)
            continue
        try:
            end = 12
            while q[end]:
                end += q[end] + 1
            question = q[12: end + 5]
            s.sendto(q[:2] + b"\x81\x80" + q[4:6] + b"\x00\x01\x00\x00\x00\x00" + question +
                     b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x3c\x00\x04" + answer_ip, addr)
        except (IndexError, OSError):
            pass


# ----------------------------------------------------------------------------
#  OTA helpers
# ----------------------------------------------------------------------------
def ota_partition():
    try:
        import esp32
        return esp32.Partition(esp32.Partition.RUNNING).get_next_update()
    except (ImportError, OSError, AttributeError):
        return None                 # firmware built without OTA partitions


def running_partition():
    try:
        import esp32
        return esp32.Partition(esp32.Partition.RUNNING).info()[4]
    except (ImportError, OSError, AttributeError):
        return ""


def fs_info():
    try:
        st = os.statvfs(ROOT or "/")
        return st[0] * st[3], st[0] * st[2]
    except OSError:
        return 0, 0


def makedirs(path):
    cur = ROOT
    for part in path.strip("/").split("/")[:-1]:
        cur += "/" + part
        try:
            os.mkdir(cur)
        except OSError:
            pass


def remove(path):
    try:
        os.remove(path)
    except OSError:
        pass


async def reboot_later(ms=1500):
    await asyncio.sleep_ms(ms)
    machine.reset()


# ----------------------------------------------------------------------------
#  System API
# ----------------------------------------------------------------------------
def make_routes(cfg, bus):
    def allowed(req, w):
        pw = cfg["admin_password"]
        if pw and req.headers.get("x-password", "") != pw:
            web.reply_json(w, {"error": "password required"}, "401 Unauthorized")
            return False
        return True

    async def system(req, w):
        free, total = fs_info()
        u = os.uname()
        part = ota_partition()
        web.reply_json(w, dict(
            platform="esp32", version=core.VERSION, micropython=u.release, machine=u.machine,
            hostname=cfg["hostname"], mode=net["mode"], ip=net["ip"], ssid=net["ssid"], mac=net["mac"], rssi=rssi(),
            mem_free=gc.mem_free(), mem_alloc=gc.mem_alloc(), fs_free=free, fs_total=total,
            uptime=time.ticks_diff(time.ticks_ms(), _boot_ms) // 1000,
            simulate=cfg["simulate"], bus=cfg["bus"], i2c=cfg["i2c"], rs485=cfg["rs485"], leds=cfg["leds"], button=cfg["button"],
            gates=[] if cfg["simulate"] else bus.scan(),
            wifi=True, ota=part is not None, partition=running_partition(),
            ap_secure=len(cfg["ap_password"]) >= 8, admin=bool(cfg["admin_password"]),
            stats=stats(),
            saved_ssid=cfg["ssid"]))

    async def scan(req, w):
        if not allowed(req, w):
            return
        best = {}
        try:
            for ssid, bssid, ch, level, auth, hidden in network.WLAN(network.STA_IF).scan():
                name = web.text(ssid)
                if name and (name not in best or level > best[name]["rssi"]):
                    best[name] = dict(ssid=name, rssi=level, secure=auth != 0)
        except OSError as e:
            print("scan failed:", e)
        web.reply_json(w, sorted(best.values(), key=lambda n: -n["rssi"]))

    async def config(req, w):
        if not allowed(req, w):
            return
        try:
            new = await req.json()
        except (ValueError, TypeError):
            return web.reply_json(w, {"error": "invalid JSON"}, "400 Bad Request")
        h = new.get("hostname", cfg["hostname"]).strip().lower()
        if not (0 < len(h) <= 32 and all(c.isalpha() or c.isdigit() or c == "-" for c in h)):
            return web.reply_json(w, {"error": "invalid hostname (a-z, 0-9, -)"}, "400 Bad Request")
        if "ap_password" in new and 0 < len(new["ap_password"]) < 8:
            return web.reply_json(w, {"error": "access point password: 8 characters minimum"}, "400 Bad Request")
        cfg["hostname"] = h
        for k in ("ssid", "password", "ap_password", "admin_password"):
            if k in new:
                cfg[k] = str(new[k])
        if "simulate" in new:
            cfg["simulate"] = bool(new["simulate"])
        if isinstance(new.get("i2c"), dict):
            for k in ("sda", "scl", "freq"):
                if k in new["i2c"]:
                    cfg["i2c"][k] = int(new["i2c"][k])
            if "soft" in new["i2c"]:
                cfg["i2c"]["soft"] = bool(new["i2c"]["soft"])
        if new.get("bus") in ("i2c", "rs485"):
            cfg["bus"] = new["bus"]
        if isinstance(new.get("button"), dict):
            b = new["button"]
            if "enabled" in b:
                cfg["button"]["enabled"] = bool(b["enabled"])
            if "pin" in b:
                cfg["button"]["pin"] = max(0, min(int(b["pin"]), 48))
            if b.get("mode") in ("button", "switch"):
                cfg["button"]["mode"] = b["mode"]
        if isinstance(new.get("leds"), dict):
            l = new["leds"]
            if "enabled" in l:
                cfg["leds"]["enabled"] = bool(l["enabled"])
            for k in ("pin", "count"):
                if k in l:
                    cfg["leds"][k] = max(0, min(int(l[k]), 64 if k == "count" else 48))
            if "brightness" in l:
                cfg["leds"]["brightness"] = max(0.05, min(float(l["brightness"]), 1.0))
        if isinstance(new.get("rs485"), dict):
            for k in ("uart", "tx", "rx", "baud", "timeout"):
                if k in new["rs485"]:
                    cfg["rs485"][k] = int(new["rs485"][k])
        save_config(cfg)
        web.reply_json(w, {"ok": True, "reboot": True})
        asyncio.create_task(reboot_later())

    async def upload(req, w):
        if not allowed(req, w):
            return
        path = req.query.get("path", "")
        if ".." in path or not (path in UPLOAD_FILES or any(path.startswith(d) and len(path) > len(d) for d in UPLOAD_DIRS)):
            return web.reply_json(w, {"error": "path refused: " + path}, "400 Bad Request")
        n = req.length
        free, total = fs_info()
        if n <= 0 or n > free - 8192:
            return web.reply_json(w, {"error": "invalid size or flash full"}, "400 Bad Request")
        dest = ROOT + path
        makedirs(path)
        tmp = dest + ".new"
        got = 0
        try:
            with open(tmp, "wb") as f:
                while got < n:
                    chunk = await req.reader.read(min(1024, n - got))
                    if not chunk:
                        raise OSError("connection lost")
                    f.write(chunk)
                    got += len(chunk)
            remove(dest)
            os.rename(tmp, dest)
            # keep one variant only, so the new file is the one used:
            #  x / x.gz (the server prefers .gz), and module.py / module.mpy (precompiled, much less RAM)
            if dest.endswith(".mpy"):
                remove(dest[:-4] + ".py")
            elif dest.endswith(".py"):
                remove(dest[:-3] + ".mpy")
            else:
                remove(dest[:-3] if dest.endswith(".gz") else dest + ".gz")
        except OSError as e:
            remove(tmp)
            return web.reply_json(w, {"error": str(e)}, "500 Internal Server Error")
        print("upload:", path, n, "bytes")
        web.reply_json(w, {"ok": True, "path": path, "size": n})

    async def firmware(req, w):
        if not allowed(req, w):
            return
        part = ota_partition()
        if part is None:
            return web.reply_json(w, {"error": "firmware without OTA partitions (use the OTA variant of MicroPython)"}, "400 Bad Request")
        n = req.length
        size = part.info()[3]
        if n <= 0 or n > size:
            return web.reply_json(w, {"error": "invalid size (%d bytes max)" % size}, "400 Bad Request")
        buf = bytearray(4096)
        block, got = 0, 0
        try:
            while got < n:
                fill = 0
                while fill < 4096 and got < n:
                    chunk = await req.reader.read(min(4096 - fill, n - got))
                    if not chunk:
                        raise OSError("connection lost")
                    buf[fill: fill + len(chunk)] = chunk
                    fill += len(chunk)
                    got += len(chunk)
                # app image: header magic 0xE9 + esp_app_desc magic 0xABCD5432 at byte 32
                # (the full USB image starts with the bootloader instead: refuse it)
                if block == 0 and (buf[0] != 0xE9 or bytes(buf[32:36]) != b"\x32\x54\xcd\xab"):
                    raise ValueError("not an ESP32 application image: send the .app-bin file")
                for i in range(fill, 4096):
                    buf[i] = 0xFF
                part.writeblocks(block, buf)       # erases then writes the 4 KB block
                block += 1
            part.set_boot()
        except (OSError, ValueError) as e:
            return web.reply_json(w, {"error": str(e)}, "500 Internal Server Error")
        print("firmware:", n, "bytes written to", part.info()[4])
        web.reply_json(w, {"ok": True, "size": n, "reboot": True})
        asyncio.create_task(reboot_later())

    async def bus_info(req, w):
        """Link counters; ?test=1: poll every address 3 times and show the raw answers."""
        info = dict(bus="simulated" if cfg["simulate"] else cfg["bus"])
        if hasattr(bus, "stats"):
            info.update(bus.stats.json())
        if req.query.get("listen"):
            if not hasattr(bus, "listen"):
                return web.reply_json(w, {"error": "listening only works on RS485"}, "400 Bad Request")
            if core.START <= core.raceState <= core.FINISH:
                return web.reply_json(w, {"error": "not during a race"}, "409 Conflict")
            ms = max(1, min(int(req.query["listen"]), 15)) * 1000
            core.paused_until = core.millis() + ms + 200       # the race loop stops polling meanwhile
            heard_before = dict(bus.stats.heard)
            info["listen"] = await bus.listen(ms)
            info["listen"]["hello"] = [a for a in bus.stats.heard if bus.stats.heard[a] != heard_before.get(a)]
            core.paused_until = 0
            info.update(bus.stats.json())
        if req.query.get("test") and hasattr(bus, "probe"):
            if core.START <= core.raceState <= core.FINISH:
                return web.reply_json(w, {"error": "not during a race"}, "409 Conflict")
            test = {}
            for a in core.address_all_gates:
                ok, raw = 0, b""
                for _ in range(3):
                    packet, data = bus.probe(a)
                    ok += 1 if packet else 0
                    raw = raw or data
                test[str(a)] = dict(ok=ok, tries=3, bytes=len(raw), raw="".join("%02x" % b for b in raw[:40]))
            info["test"] = test
        web.reply_json(w, info)

    async def reboot(req, w):
        if not allowed(req, w):
            return
        web.reply_json(w, {"ok": True})
        asyncio.create_task(reboot_later(500))

    return {("GET", "/api/system"): system, ("GET", "/api/scan"): scan,
            ("POST", "/api/config"): config, ("POST", "/api/upload"): upload,
            ("POST", "/api/firmware"): firmware, ("POST", "/api/reboot"): reboot,
            ("GET", "/api/bus"): bus_info}


async def captive(req, w):
    """AP mode: unknown URLs (phone connectivity checks) -> system page."""
    if net["mode"] != "ap":
        return False
    web.reply(w, "302 Found", "", extra="Location: http://%s/system.html\r\n" % net["ip"])
    return True


_idf_min = [None]


def stats():
    """Resources for the debug panel. RAM = MicroPython heap; idf = ESP-IDF heap (Wi-Fi, sockets...)."""
    free, total = fs_info()
    d = dict(ram_free=gc.mem_free(), ram_used=gc.mem_alloc(), flash_free=free, flash_total=total,
             cpu=web.cpu_load, uptime=time.ticks_diff(time.ticks_ms(), _boot_ms) // 1000)
    try:
        d["mhz"] = machine.freq() // 1000000
    except (AttributeError, OSError):
        pass
    try:
        import micropython
        d["stack"] = micropython.stack_use()
    except (ImportError, AttributeError):
        pass
    try:
        import esp32
        regions = esp32.idf_heap_info(esp32.HEAP_DATA)      # (total, free, largest free block, min free) per region
        d["idf_free"] = sum(r[1] for r in regions)
        d["idf_total"] = sum(r[0] for r in regions)
        d["idf_largest"] = max(r[2] for r in regions)
        d["idf_min"] = sum(r[3] for r in regions)             # lowest free since boot
    except (ImportError, AttributeError, ValueError):
        pass
    return d


async def gc_loop():
    """Collect regularly so the heap stays unfragmented; less often during a race (a collection pauses
    the loop a few tens of ms; the lap times are not affected: the gates timestamp the passes)."""
    while True:
        await asyncio.sleep(10 if core.START <= core.raceState <= core.FINISH else 5)
        gc.collect()


# ----------------------------------------------------------------------------
#  Entry point
# ----------------------------------------------------------------------------
def run():
    try:
        machine.freq(240000000)         # MicroPython boots the ESP32 at 160 MHz: +50 % CPU for the race loop
    except (AttributeError, ValueError, OSError):
        pass
    cfg = load_config()
    wifi_start(cfg)
    if cfg["simulate"]:
        from ricino import sim
        bus = sim.install()
        print("demo mode: simulated gates and cars")
    elif cfg["bus"] == "rs485":
        bus = RS485Bus(cfg["rs485"])
        print("RS485 gates found:", bus.scan())
    else:
        bus = I2CBus(cfg["i2c"])
        print("I2C gates found:", bus.scan())
    if cfg["leds"]["enabled"]:
        try:
            from ricino import lights
            l = cfg["leds"]
            core.loop_hooks.append(lights.Strip(l["pin"], l["count"], l["brightness"]).loop)
            print("start lights: %d LEDs on GPIO %d" % (l["count"], l["pin"]))
        except Exception as e:      # wrong pin / no neopixel module: run without lights
            print("start lights disabled:", e)
    if cfg["button"]["enabled"]:
        try:
            from ricino import button
            pin = machine.Pin(cfg["button"]["pin"], machine.Pin.IN, machine.Pin.PULL_UP)
            core.loop_hooks.append(button.Button(lambda: pin.value() == 0, cfg["button"]["mode"]).loop)
            print("start/stop %s on GPIO %d" % (cfg["button"]["mode"], cfg["button"]["pin"]))
        except Exception as e:
            print("start/stop button disabled:", e)
    clock = Clock()
    server = web.Server(ROOT + DATA_DIR, make_routes(cfg, bus), captive)
    def load():
        with open(ROOT + SETTINGS) as f:
            return json.load(f)

    def save(d):
        path = ROOT + SETTINGS
        with open(path + ".new", "w") as f:
            json.dump(d, f)
        remove(path)
        os.rename(path + ".new", path)

    def load_rec():
        with open(ROOT + RECORDS) as f:
            return json.load(f)

    def save_rec(d):
        path = ROOT + RECORDS
        with open(path + ".new", "w") as f:
            json.dump(d, f)
        remove(path)
        os.rename(path + ".new", path)

    core.platform_stats = stats
    core.VERBOSE = False            # no log line per pass on the console UART
    core.setup(bus, clock.millis, clock.elapsed_us, lambda: str(gc.mem_free()), server.broadcast, server.send_to,
               1.0, load, save, load_rec, save_rec)

    async def main():
        await server.start(cfg["port"])
        if net["mode"] == "ap":
            asyncio.create_task(dns_server(net["ip"]))
        asyncio.create_task(gc_loop())
        try:                        # we came up fine: keep this firmware (cancels the OTA rollback)
            import esp32
            esp32.Partition.mark_app_valid_cancel_rollback()
        except (ImportError, OSError, AttributeError):
            pass
        print("RicinoNext %s ready" % core.VERSION)
        await web.run_loop(0.005)          # 200 loops/s: gates are polled every 20 ms anyway

    asyncio.run(main())
