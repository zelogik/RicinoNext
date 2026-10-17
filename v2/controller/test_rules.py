#!/usr/bin/env python3
"""Lap counting rules of ricino/core.py: debounce per gate, gate order, skipped-gate tolerance."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ricino import core   # noqa: E402

events = []
core._broadcast = lambda s: events.append(s)
G = core.address_all_gates                                   # 21, 22, 23 ...


def car(gates=3, tolerance=10, debounce=3000):
    core.ui_config.gates, core.ui_config.tolerance, core.ui_config.min_pass_ms = gates, tolerance, debounce
    core.ui_config.start = core.FLYING
    d = core.IDDataSorted()
    d.ID = 4242
    return d


def run(d, passes):                                          # (time ms, gate index)
    for t, g in passes:
        d.update_time(t, G[g])

# 1. debounce: 1 gate, detections closer than 3 s ignored, and they extend the silence needed
d = car(gates=1)
run(d, [(1000, 0), (1500, 0), (5000, 0), (6000, 0), (7500, 0), (11000, 0)])
# armed at 1000; 1500 ignored; 5000 counts (lap 4000); 6000 ignored; 7500 ignored (only 1.5 s after 6000); 11000 counts
assert d.laps == 2 and d.history == [4000, 6000], (d.laps, d.history)
print("debounce   : OK", d.history)

# 2. order: 0 -> 1 -> 2 -> 0 counts, sectors measured; detections before the first finish crossing ignored
d = car()
run(d, [(500, 2), (1000, 0), (3000, 1), (5000, 2), (7000, 0)])
assert d.laps == 1 and d.last_lap_time == 6000 and d.last_check_point[:3] == [2000, 2000, 2000], (d.laps, d.last_check_point)
print("order      : OK lap", d.last_lap_time, "sectors", d.last_check_point[:3])

# 3. skipped gate tolerated once, then refused within the window, then tolerated again after N clean passes
events.clear()
run(d, [(9000, 1), (13000, 0)])                               # gate 3 (idx 2) missed -> tolerated
assert d.laps == 2 and d.missed == 1 and d.cuts == 0 and d.last_check_point[0] == 2000, (d.laps, d.missed, d.last_check_point)
assert '"type":"miss"' in events[-1] and '"gate":3' in events[-1], events
run(d, [(15000, 1), (19000, 0)])                              # missed again, error in the last 10 passes -> lap refused
assert d.laps == 2 and d.cuts == 1 and '"type":"cut"' in events[-1], (d.laps, d.cuts, events[-1])
run(d, [(21000, 1), (23000, 2), (25000, 0)])                  # next clean lap counts, measured from 19000
assert d.laps == 3 and d.last_lap_time == 6000, (d.laps, d.last_lap_time)
t = 25000
for _ in range(4):                                            # 12 more clean passes
    run(d, [(t + 2000, 1), (t + 4000, 2), (t + 6000, 0)])
    t += 6000
run(d, [(t + 2000, 1), (t + 6000, 0)])                        # a skip, after 10+ clean passes: tolerated again
assert d.laps == 8 and d.missed == 2 and d.cuts == 1, (d.laps, d.missed, d.cuts)
print("tolerance  : OK laps", d.laps, "missed", d.missed, "refused", d.cuts)

# 4. a car stopped on the line (detected every second) never counts laps
d = car()
run(d, [(1000 + i * 1000, 0) for i in range(20)])
assert d.laps == 0, d.laps
# 5. tolerance 0: every skipped gate refuses the lap
d = car(tolerance=0)
run(d, [(1000, 0), (3000, 1), (7000, 0), (9000, 1), (11000, 2), (13000, 0)])
assert d.laps == 1 and d.cuts == 1 and d.last_lap_time == 6000, (d.laps, d.cuts)
print("strict     : OK")
print("OK")
