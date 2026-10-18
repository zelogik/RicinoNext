#!/usr/bin/env python3
"""Emitter tests on a PC.

1. protocol: src/protocol.h builds the same frame and EEPROM bytes as the first firmware
   (Software/Emitter/irda_robi) for 2.6 million IDs, and the CRC of real Robitronic transponders.
2. firmware in a simulator (simavr, from PlatformIO: pio pkg install -g -t platformio/tool-simavr):
   the built .elf runs 2-3 s of simulated time; the IR pin is measured to the CPU cycle and every frame
   is decoded. Needs a build first (pio run -e attiny24 -e attiny85); skipped if simavr is missing.

Run: python3 emitter/test/test_emitter.py
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SIMAVR = os.path.expanduser("~/.platformio/packages/tool-simavr")
TMP = tempfile.mkdtemp()


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        sys.exit("FAILED: %s\n%s%s" % (" ".join(cmd), p.stdout, p.stderr))
    return p.stdout


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def image(id_, byte4="ff"):
    """EEPROM bytes 0..5 as hex, as the first firmware writes them."""
    b = [id_ & 0xFF, (id_ >> 8) & 0xFF, id_ >> 16]
    b.append(crc8(b))
    parity = 0
    for x in b:
        parity = (parity << 1) | (1 ^ (bin(x).count("1") & 1))
    return "".join("%02x" % x for x in b) + byte4 + "%02x" % parity


# ---- 1. protocol ---------------------------------------------------------------------------------
exe = os.path.join(TMP, "host_protocol")
run(["cc", "-O2", "-Wall", "-Werror", "-o", exe, os.path.join(HERE, "host_protocol.c")])
print(run([exe]).strip())

# ---- 2. firmware in simavr ---------------------------------------------------------------------------
if not os.path.isdir(SIMAVR):
    print("simulator  : skipped (pio pkg install -g -t platformio/tool-simavr)")
    sys.exit(0)
sim = os.path.join(TMP, "sim_timing")
run(["cc", "-O2", "-Wall", "-o", sim, os.path.join(HERE, "sim_timing.c"), "-I" + SIMAVR + "/include/simavr",
     "-L" + SIMAVR + "/lib", "-lsimavr", "-Wl,-rpath," + SIMAVR + "/lib"])


def simulate(mcu, seconds, eeprom, key):
    elf = os.path.join(ROOT, ".pio", "build", mcu, "firmware.elf")
    if not os.path.exists(elf):
        sys.exit("FAILED: %s not built (cd emitter && pio run -e %s)" % (elf, mcu))
    out = {}
    for line in run([sim, elf, mcu, str(seconds), eeprom, "1" if key else "0"]).splitlines():
        k, *v = line.split()
        if k != "Loaded":
            out[k] = v
    return out


def check(mcu, name, r, expect_id=None, new_id=False, old=None):
    assert r["pulse_cycles"] == ["16", "16"], (name, r)                    # 2 us
    f = r["frames"]                                                         # N bad B ids I id HEX
    frames, bad, ids, id_ = int(f[0]), int(f[2]), int(f[4]), int(f[6], 16)
    assert frames > 500 and bad == 0 and ids == 1, (name, r)               # every frame decodes, one ID
    gmin, gmax, gmean = map(float, r["gap_us"])
    assert 900 <= gmin and gmax <= 4500 and 2000 <= gmean <= 3300, (name, r)
    if expect_id is not None:
        assert id_ == expect_id, (name, hex(id_))
    if new_id:
        assert 0 < id_ <= 0xFFFFFE and (old is None or id_ != old), (name, hex(id_))
    assert r["eeprom"][0] == image(id_), (name, r["eeprom"], image(id_))  # EEPROM = what the frame says
    print("%-11s: OK %-28s id 0x%06x, %d frames, all valid, pulse 2 us, gap %.0f-%.0f us (mean %.0f)"
          % (mcu, name, id_, frames, gmin, gmax, gmean))


CAR = 6427                                     # a car flashed with the first firmware
for mcu in ("attiny24", "attiny85"):
    check(mcu, "existing car (ID kept)", simulate(mcu, 2, image(CAR), False), expect_id=CAR)
    check(mcu, "new-ID key held", simulate(mcu, 2, image(CAR), True), new_id=True, old=CAR)
check("attiny24", "blank chip (new ID)", simulate("attiny24", 2, "-", False), new_id=True)
broken = image(CAR)[:6] + "00ff00"             # CRC and parity damaged: repaired, ID kept
check("attiny24", "damaged EEPROM (repaired)", simulate("attiny24", 2, broken, False), expect_id=CAR)
print("OK")
