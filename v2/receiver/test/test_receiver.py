#!/usr/bin/env python3
"""Runs the receiver firmware on the PC (host.cpp + Arduino stub) and talks to it with the
controller's own code (controller/ricino/rs485.py and core.py): checks that both sides agree.

  python3 receiver/test/test_receiver.py        (needs g++)
"""
import os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "controller"))
from ricino import core, rs485   # noqa: E402

BIN = os.path.join(tempfile.mkdtemp(prefix="ricino-rx-"), "rx_host")
subprocess.check_call(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", os.path.join(HERE, "stub"),
                       "-o", BIN, os.path.join(HERE, "host.cpp")])


class Gate:
    def __init__(self, jumpers=0):
        self.p = subprocess.Popen([BIN, str(jumpers)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def cmd(self, line, answer=False):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        return self.p.stdout.readline().strip() if answer else None

    def run(self, n=3, ms=1):
        for _ in range(n):
            self.cmd("t %d" % ms)
            self.cmd("loop 1")

    def bus(self, data):
        self.cmd("bus " + data.hex())
        self.run()
        return bytes.fromhex(self.cmd("out", True))

    def decoder_out(self):
        return bytes.fromhex(self.cmd("dout", True))

    def poll(self, addr, cmd=rs485.POLL):
        raw = self.bus(rs485.request(addr, cmd))
        return raw, (rs485.parse_reply(raw, addr) if raw else None)


def car_frame(ID, ms, hit=12, strength=200):
    """Robitronic decoder frame: 0D | checksum | 84 | ID[4] LE | time[4] LE | hits | strength."""
    return bytes([0x0D, 0x00, 0x84]) + struct.pack("<II", ID, ms) + bytes([hit, strength])


def time_frame(ms):
    return bytes([0x0B, 0x00, 0x83]) + struct.pack("<I", ms) + bytes([0x14, 0xD0, 0x01, 0x02])


def status(pkt):
    assert pkt[0] == 0x82 and core.crc8(pkt[3:13]) == pkt[2], pkt.hex()
    return dict(state=pkt[3], err_decoder=pkt[7], err_queue=pkt[8], err_link=pkt[9], queued=pkt[10],
                version=pkt[11], flags=pkt[12])


def next_status(g, addr):
    for _ in range(4):
        raw, pkt = g.poll(addr)
        if pkt and pkt[0] == 0x82:
            return status(pkt)
    raise AssertionError("no status packet")


g = Gate(jumpers=1)                                     # SJ3 closed -> address 22
assert g.cmd("addr", True) == "22"
hello = bytes.fromhex(g.cmd("out", True))               # unasked frame at power-up (transmit path check)
hp = rs485.parse_reply(hello, 22)
assert hp and hp[0] == 0x82 and hp[1] == 22, hello.hex()
print("hello      : OK", hello.hex())
assert g.decoder_out() == bytes([0x03, 0xB0, 0x02]), "decoder reset at boot"
print("address    : OK (SJ3 -> 22)")

# poll / addressing / bad CRC
raw, pkt = g.poll(22)
st = status(pkt)
assert st["version"] == 3, st
assert g.poll(21) == (b"", None), "another address must stay silent"
bad = bytearray(rs485.request(22, rs485.POLL))
bad[3] ^= 0x55
assert g.bus(bytes(bad)) == b"", "bad CRC: no answer"
assert next_status(g, 22)["err_link"] >= 1
print("poll       : OK", st)

# passes ignored when not racing
g.cmd("dec " + car_frame(4242, 1000).hex())
g.run()
assert next_status(g, 22)["queued"] == 0
# broadcast start -> decoder started, no answer on the bus
assert g.decoder_out() == bytes([0x03, 0xB0, 0x02]), "decoder reset when the controller appears"
assert g.bus(rs485.request(0, 0x8A)) == b""
assert g.decoder_out() == bytes([0x03, 0xB9, 0x01]), "decoder start frame"
assert next_status(g, 22)["state"] == 3

# passes: order kept, packets accepted by the controller code
g.cmd("dec " + car_frame(4242, 5123).hex() + time_frame(5500).hex() + car_frame(1337, 5600, 7, 99).hex())
g.run()
got = []
for _ in range(6):
    raw, pkt = g.poll(22)
    if pkt[0] == 0x84:
        assert core.crc8(pkt[3:13]) == pkt[2], "controller CRC check"
        got.append(struct.unpack_from("<II", pkt, 3) + (pkt[1], pkt[11], pkt[12]))
assert got == [(4242, 5123, 22, 12, 200), (1337, 5600, 22, 7, 99)], got
print("passes     : OK", got)

# lost reply: REPEAT gives the same bytes, then polling resumes
g.cmd("dec " + car_frame(2468, 7000).hex())
g.run()
raw1, p1 = g.poll(22)
raw2, p2 = g.poll(22, rs485.REPEAT)
assert raw1 == raw2 and p1[0] == 0x84, "repeat = identical reply"
print("repeat     : OK")

# decoder errors: truncated frame, implausible length byte, then a valid frame still decoded
g.cmd("dec 0d0084aabb")
g.run(2, 10)                                           # 20 ms gap inside the frame
g.cmd("dec ff01" + car_frame(8080, 8000).hex())
g.run()
pk = [g.poll(22)[1] for _ in range(3)]
assert any(p[0] == 0x84 and struct.unpack_from("<I", p, 3)[0] == 8080 for p in pk), [p.hex() for p in pk]
st = next_status(g, 22)
assert st["err_decoder"] >= 3, st
print("decoder err: OK", st)

# queue overflow: 10 passes without any poll -> 8 kept in order, 2 counted
g.cmd("dec " + "".join(car_frame(100 + i, 9000 + i).hex() for i in range(10)))
g.run()
ids = []
for _ in range(12):
    p = g.poll(22)[1]
    if p[0] == 0x84:
        ids.append(struct.unpack_from("<I", p, 3)[0])
st = next_status(g, 22)
assert ids == list(range(100, 108)) and st["err_queue"] == 2, (ids, st)
print("queue      : OK (8 kept, 2 dropped)")

# link lost during the race: decoder NOT restarted, race goes on, flag set
g.decoder_out()
g.cmd("t 3000")
g.cmd("dec " + time_frame(12000).hex())                 # decoder alive
g.run()
assert g.decoder_out() == b"", "no decoder command while the link is down"
g.cmd("dec " + car_frame(4242, 12500).hex())
g.run()
raw, pkt = g.poll(22)
assert pkt[0] == 0x84, "pass made during the outage delivered"
st = next_status(g, 22)
assert st["state"] == 3, st
print("link lost  : OK (race kept, pass delivered)")

# decoder silent for 3 s in race -> flag bit 0
g.cmd("t 3500")
g.run()
st = next_status(g, 22)
assert st["flags"] & 1, st
print("decoder    : OK (silence flagged)")

# stop (addressed) -> decoder reset, idle
assert g.bus(rs485.request(22, 0x8F)) == b""
assert g.decoder_out() == bytes([0x03, 0xB0, 0x02])
assert next_status(g, 22)["state"] == 1
print("stop       : OK")
g.p.stdin.close()
g.p.wait()
print("OK")
