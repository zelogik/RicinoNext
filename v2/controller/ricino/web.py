"""Tiny HTTP + WebSocket server on asyncio streams, for CPython and MicroPython.

- static files from a data directory (like serveStatic on SPIFFS), "<file>.gz" preferred,
  sent in chunks so a small heap is enough;
- routes: {("GET", "/api/x"): async handler(req, w)} for the platform layer (system page, OTA);
- WebSocket on /ws: text frames to/from ricino.core. Each client has its own send queue
  drained by one task (MicroPython: only one task may write a socket).

CPython / MicroPython common subset only (no os.path, no readuntil, no errors= in decode).
"""
import asyncio, binascii, hashlib, json, os, struct, time
from ricino import core

try:                                    # MicroPython: wrapping microsecond ticks
    _us, _diff = time.ticks_us, time.ticks_diff
except AttributeError:                  # CPython
    def _us():
        return int(time.perf_counter() * 1000000)

    def _diff(a, b):
        return a - b

cpu_load = 0                            # % of the time the event loop is busy (race loop + web), last second

GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
CHUNK = 1024
MIME = {"html": "text/html; charset=utf-8", "js": "text/javascript", "css": "text/css", "json": "application/json",
        "svg": "image/svg+xml", "ico": "image/x-icon", "png": "image/png", "txt": "text/plain"}


def file_size(path):
    try:
        st = os.stat(path)
        return -1 if st[0] & 0x4000 else st[6]       # directory -> -1
    except OSError:
        return -1


def text(b):
    try:
        return b.decode()
    except UnicodeError:
        return ""


class Request:
    def __init__(self, method, path, query, headers, reader):
        self.method, self.path, self.query, self.headers, self.reader = method, path, query, headers, reader

    @property
    def length(self):
        try:
            return int(self.headers.get("content-length", "0"))
        except ValueError:
            return 0

    async def body(self, limit=8192):
        n = self.length
        if n > limit:
            raise ValueError("body too large")
        return await self.reader.readexactly(n) if n else b""

    async def json(self):
        return json.loads(text(await self.body()))


def reply(w, status, body=b"", ctype="text/plain", extra=""):
    if isinstance(body, str):
        body = body.encode()
    w.write(("HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-cache\r\n%sConnection: close\r\n\r\n"
             % (status, ctype, len(body), extra)).encode() + body)


def reply_json(w, obj, status="200 OK"):
    reply(w, status, core.dumps(obj), "application/json")


def parse_query(qs):
    q = {}
    for part in qs.split("&"):
        if "=" in part:
            k, v = part.split("=", 1)
            q[unquote(k)] = unquote(v)
    return q


def unquote(s):
    s = s.replace("+", " ")
    if "%" not in s:
        return s
    out, i = bytearray(), 0
    b = s.encode()
    while i < len(b):
        if b[i] == 0x25 and i + 2 < len(b):         # '%XX'
            try:
                out.append(int(text(b[i + 1: i + 3]), 16))
                i += 3
                continue
            except ValueError:
                pass
        out.append(b[i])
        i += 1
    return text(bytes(out))


class WSClient:
    def __init__(self, w):
        self.w, self.queue, self.alive = w, [], True
        self.event = asyncio.Event()

    def send(self, data, opcode=0x1):
        if not self.alive:
            return
        n = len(data)
        if n < 126:
            hdr = bytes([0x80 | opcode, n])
        elif n < 65536:
            hdr = bytes([0x80 | opcode, 126]) + struct.pack(">H", n)
        else:
            hdr = bytes([0x80 | opcode, 127]) + struct.pack(">Q", n)
        self.queue.append(hdr + data)
        if len(self.queue) > 64:          # slow client: drop the oldest frames instead of eating the heap
            self.queue.pop(0)
        self.event.set()

    async def sender(self):
        try:
            while self.alive:
                await self.event.wait()
                self.event.clear()
                while self.queue:
                    self.w.write(self.queue.pop(0))
                    await self.w.drain()
        except Exception:
            self.alive = False


class Server:
    def __init__(self, data_dir, routes=None, not_found=None):
        self.data_dir = data_dir.rstrip("/")
        self.routes = routes or {}
        self.not_found = not_found      # optional async handler(req, w) -> True if handled (captive portal)
        self.clients = []

    # ---- core hooks ----------------------------------------------------------------
    def broadcast(self, s):
        data = s.encode()
        for c in self.clients:
            c.send(data)

    def send_to(self, client, s):
        client.send(s.encode())

    # ---- HTTP ------------------------------------------------------------------------
    async def handle(self, r, w):
        try:
            line = text(await r.readline()).strip()
            if not line:
                return
            parts = line.split(" ")
            if len(parts) < 2:
                return
            method, target = parts[0], parts[1]
            headers = {}
            while True:
                h = text(await r.readline()).strip()
                if not h:
                    break
                if ":" in h:
                    k, v = h.split(":", 1)
                    headers[k.strip().lower()] = v.strip()
            path, qs = (target.split("?", 1) + [""])[:2]
            req = Request(method, path, parse_query(qs), headers, r)
            if path == "/ws" and "sec-websocket-key" in headers:
                await self.websocket(req, r, w)
                return
            handler = self.routes.get((method, path))
            if handler:
                await handler(req, w)
            elif method == "GET" and await self.static(req, w):
                pass
            elif self.not_found and await self.not_found(req, w):
                pass
            else:
                reply(w, "404 Not Found", "not found")
            await w.drain()
        except Exception as e:
            print("http error:", repr(e))
        finally:
            try:
                w.close()
                await w.wait_closed()
            except Exception:
                pass

    async def static(self, req, w):
        path = req.path
        if path.endswith("/"):
            path += "index.html"
        if ".." in path or "\\" in path:
            return False
        full = self.data_dir + path
        ext = path.rsplit(".", 1)[-1].lower() if "." in path else ""
        ctype = MIME.get(ext, "application/octet-stream")
        extra = ""
        size = -1
        if "gzip" in req.headers.get("accept-encoding", ""):      # like serveStatic: prefer .gz
            size = file_size(full + ".gz")
            if size >= 0:
                full += ".gz"
                extra = "Content-Encoding: gzip\r\n"
        if size < 0:
            size = file_size(full)
        if size < 0:
            return False
        w.write(("HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-cache\r\n%s"
                 "Connection: close\r\n\r\n" % (ctype, size, extra)).encode())
        with open(full, "rb") as f:
            while True:
                chunk = f.read(CHUNK)
                if not chunk:
                    break
                w.write(chunk)
                await w.drain()
        return True

    # ---- WebSocket ---------------------------------------------------------------------
    async def websocket(self, req, r, w):
        key = req.headers["sec-websocket-key"].encode()
        accept = binascii.b2a_base64(hashlib.sha1(key + GUID).digest()).strip()
        w.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        await w.drain()
        client = WSClient(w)
        self.clients.append(client)
        task = asyncio.create_task(client.sender())
        print("WebSocket client connected (%d)" % len(self.clients))
        try:
            core.conf_to_json(client)
            while True:
                b1, b2 = await r.readexactly(2)
                n = b2 & 0x7F
                if n == 126:
                    n = struct.unpack(">H", await r.readexactly(2))[0]
                elif n == 127:
                    n = struct.unpack(">Q", await r.readexactly(8))[0]
                if n > 8192:                                   # conf with 8 names is ~1 KB
                    break
                mask = await r.readexactly(4) if b2 & 0x80 else b"\0\0\0\0"
                payload = bytearray(await r.readexactly(n))
                for i in range(n):
                    payload[i] ^= mask[i & 3]
                op = b1 & 0x0F
                if op == 0x8:                                  # close
                    break
                if op == 0x9:                                  # ping -> pong
                    client.send(bytes(payload), 0xA)
                elif op == 0x1 and b1 & 0x80:
                    core.on_message(text(bytes(payload)))
                elif op == 0x1:
                    print("Received a ws message, but it didn't fit into one frame")
        except Exception:
            pass
        finally:
            client.alive = False
            client.event.set()
            if client in self.clients:
                self.clients.remove(client)
            print("WebSocket client disconnected (%d)" % len(self.clients))
            try:
                await task
            except Exception:
                pass

    async def start(self, port, host="0.0.0.0"):
        return await asyncio.start_server(self.handle, host, port)


async def run_loop(period_s=0.001):
    """Calls core.loop() forever (the Arduino loop()), and estimates the CPU load: time spent in
    core.loop() + the delay other tasks (HTTP, WebSocket) add to our sleep, over the elapsed time."""
    global cpu_load
    period_us = int(period_s * 1000000)
    busy, win = 0, _us()
    while True:
        t0 = _us()
        core.loop()
        t1 = _us()
        await asyncio.sleep(period_s)
        t2 = _us()
        busy += _diff(t1, t0) + max(0, _diff(t2, t1) - period_us)
        span = _diff(t2, win)
        if span >= 1000000:
            cpu_load = min(100, busy * 100 // span)
            busy, win = 0, t2
