#!/usr/bin/env python3
"""End-to-end test: start controller.py (x10 speed), run a laps race and a time race over a raw WebSocket."""
import json, os, socket, struct, subprocess, sys, tempfile, time

PORT = 18090
HERE = os.path.dirname(os.path.abspath(__file__))


class WS:
    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
        self.s.sendall(b"GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n")
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            self.buf += self.s.recv(1024)
        assert b" 101 " in self.buf.split(b"\r\n")[0]
        self.buf = self.buf.split(b"\r\n\r\n", 1)[1]

    def _read(self, n):
        while len(self.buf) < n:
            self.buf += self.s.recv(65536)
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self):
        _, b2 = self._read(2)
        n = b2 & 0x7F
        if n == 126:
            n = struct.unpack(">H", self._read(2))[0]
        return json.loads(self._read(n))

    def send(self, conf):
        d, m = json.dumps({"conf": conf}).encode(), b"\x01\x02\x03\x04"
        hdr = bytes([0x81, 0x80 | len(d)]) if len(d) < 126 else bytes([0x81, 0x80 | 126]) + struct.pack(">H", len(d))
        self.s.sendall(hdr + m + bytes(c ^ m[i % 4] for i, c in enumerate(d)))


def run_race(ws, timeout):
    lives, states, msgs = [], [], []
    end = time.time() + timeout
    while time.time() < end:
        m = ws.recv()
        if "live" in m:
            lives.append(m["live"])
        if m.get("race", {}).get("state") == "WARMUP":
            assert 0 <= m["race"]["countdown"] <= 30000, m
        if "race" in m and "state" in m["race"]:
            if not states or states[-1] != m["race"]["state"]:
                states.append(m["race"]["state"])
            if "message" in m["race"]:
                msgs.append(m["race"]["message"])
            if states[-1] == "WAIT" and "RACE" in states:
                return lives, states, msgs
    raise AssertionError(f"race did not finish: {states}")


SETTINGS = os.path.join(tempfile.mkdtemp(prefix="ricino-test-"), "settings.json")


def start():
    return subprocess.Popen([sys.executable, os.path.join(HERE, "main.py"), "--port", str(PORT), "--speed", "10",
                             "--laptime", "5", "--settings", SETTINGS], stdout=subprocess.DEVNULL)


p = start()
try:
    time.sleep(0.8)
    html = socket.create_connection(("127.0.0.1", PORT))
    html.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
    assert b"200 OK" in html.recv(200), "index.html not served"

    def http(method, path, body=b""):
        c = socket.create_connection(("127.0.0.1", PORT))
        c.sendall(b"%s %s HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\n\r\n" % (method, path, len(body)) + body)
        data = b""
        while chunk := c.recv(65536):
            data += chunk
        head, _, payload = data.partition(b"\r\n\r\n")
        return int(head.split()[1]), payload

    st, body = http(b"GET", b"/api/system")
    assert st == 200 and json.loads(body)["platform"] == "pc", body
    st, body = http(b"GET", b"/system.html")
    assert st == 200 and b"Syst" in body
    st, body = http(b"POST", b"/api/upload?path=/ricino/core.py", b"x")
    assert st == 403, st
    print("system page: OK (pc)")

    ws = WS()
    first = ws.recv()
    assert first["conf"]["laps_m"] == 100 and len(first["conf"]["names"]) == 4, first

    # tag reading mode: finish-line gate listens outside races, passes come as {"tag": ...}
    ws.send({"read": "1"})
    tags, conf_read, end = [], None, time.time() + 10
    while time.time() < end and len(tags) < 2:
        m = ws.recv()
        if "conf" in m:
            conf_read = m["conf"]["read"]
        if "tag" in m:
            tags.append(m["tag"])
        assert "live" not in m, "no timing while reading tags"
    assert conf_read == 1 and len(tags) >= 2 and all(t["gate"] == 21 and t["id"] > 0 for t in tags), (conf_read, tags)
    ws.send({"read": "0"})
    while "conf" not in (m := ws.recv()):
        pass
    assert m["conf"]["read"] == 0, m
    # a race start also ends the reading mode
    ws.send({"read": "1"})
    ws.send({"state": "1"})
    while not ("conf" in (m := ws.recv()) and m["conf"]["state"] == 1):
        pass
    assert m["conf"]["read"] == 0, m
    ws.send({"state": "0"})
    print("tag reading: OK", [t["id"] for t in tags])

    # laps race
    ws.send({"laps": "3", "players": "3", "style": "0", "warmup": "5"})
    ws.send({"state": "1"})
    lives, states, msgs = run_race(ws, 30)
    final = {}
    for l in lives:
        final[l["rank"]] = l
    print("laps race  :", states, msgs)
    print("  ranking  :", [(r, final[r]["id"], final[r]["lap"], final[r]["total"]) for r in sorted(final)])
    # a client connecting after the race gets the lap history and the arcade records
    late = WS().recv()
    assert len(late["records"]) == 3 and all(r["laps"] == 3 and r["race_laps"] == 3 for r in late["records"]), late["records"]
    print("records    : OK", sorted(r["best"] for r in late["records"]))
    assert sorted(len(v) for v in late["history"].values()) == [3, 3, 3], late.get("history")
    print("history    : OK")
    assert ["WARMUP", "RACE", "FINISH"] == [s for s in states if s in ("WARMUP", "RACE", "FINISH")]
    assert max(l["lap"] for l in lives) == 3 and all(l["last"] > 0 for l in lives if l["lap"])
    assert all(len(l["gate"]) == 3 for l in lives)
    # rolling start: own time starts at the first crossing, so total < race time of the crossing
    assert all(0 < l["total"] < l["cross"] for l in lives if l["lap"] >= 1), lives[:3]

    # standing start: every clock runs from the green flag, so total == race time of the crossing
    ws.send({"start": "0", "laps": "2"})
    ws.send({"state": "1"})
    lives, states, msgs = run_race(ws, 30)
    assert all(l["total"] == l["cross"] for l in lives if l["lap"] >= 1), lives[:3]
    print("standing   : OK")
    ws.send({"start": "1"})

    # time race (1 min min, x10 -> 6 s)
    ws.send({"style": "1", "time": "60000"})
    ws.send({"state": "1"})
    lives, states, msgs = run_race(ws, 30)
    print("time race  :", states, msgs)
    assert "FINISH" in states and lives
    # driver names (array sent as JSON objects, id as number)
    ws.send({"names": [{"id": 4242, "name": "Cedric", "color": "#112233"}]})
    while "conf" not in (m := ws.recv()):
        pass
    assert m["conf"]["names"][0] == {"id": 4242, "name": "Cedric", "color": "#112233"}, m
    print("names      : OK")

    # garage: 50 cars max, sent with the conf only when it changed (and at connection)
    ws.send({"cars": [{"id": 5000 + i, "name": "Car %d" % i} for i in range(60)]})
    while "conf" not in (m := ws.recv()):
        pass
    assert len(m["conf"]["cars"]) == 50, len(m["conf"].get("cars", []))
    ws.send({"laps": "3"})
    while "conf" not in (m := ws.recv()):
        pass
    assert "cars" not in m["conf"], "unchanged garage is not resent"
    assert len(WS().recv()["conf"]["cars"]) == 50, "a new client gets the garage"
    print("garage     : OK (50 max, sent on change only)")

    # free mode: never ends by itself, manual stop
    ws.send({"style": "2"})
    ws.send({"state": "1"})
    lives, end = [], time.time() + 15
    while time.time() < end and max([l["lap"] for l in lives] or [0]) < 6:
        m = ws.recv()
        if "live" in m:
            lives.append(m["live"])
        assert m.get("race", {}).get("state") != "FINISH", "free mode must not finish"
    assert max(l["lap"] for l in lives) >= 6, "free mode: laps beyond the laps condition"
    ws.send({"state": "0"})
    print("free mode  : OK (%d laps)" % max(l["lap"] for l in lives))

    # settings survive a restart (saved 3 s after the last change; x10 clock)
    time.sleep(1)
    p.terminate(); p.wait()
    p = start()
    time.sleep(0.8)
    c = WS().recv()["conf"]
    assert c["names"][0]["name"] == "Cedric" and c["style"] == 2 and c["players"] == 3, c
    first = WS()
    first.recv()
    first.send({"record_del": str(min(r["uid"] for r in late["records"]))})
    while "records" not in (m := first.recv()):
        pass
    assert len(m["records"]) >= 3, "records kept after restart (one deleted, later races added some)"
    print("persistence: OK")
    print("OK")
finally:
    p.terminate()
