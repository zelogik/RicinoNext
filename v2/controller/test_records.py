#!/usr/bin/env python3
"""Unit test of ricino/records.py (arcade top 10)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ricino.records import Records, TOP   # noqa: E402

R = Records()
def res(name, laps, best, mean, total, race_laps=5):
    return dict(at=0, id=hash(name) & 0xffff, name=name, color="", laps=laps, best=best, mean=mean, total=total,
                race_laps=race_laps)

news = R.add([res("A", 5, 4000, 4300, 21500), res("B", 5, 3900, 4400, 22000), res("C", 2, 3800, 4100, 0)])
assert [r["name"] for r in R.ranking("best")] == ["C", "B", "A"]
assert [r["name"] for r in R.ranking("mean")] == ["A", "B"], "C has too few laps for the mean table"
assert [r["name"] for r in R.ranking("total", 5)] == ["A", "B"], "C did not finish the 5 laps"
assert {(n["name"], n["kind"], n["rank"]) for n in news} >= {("C", "best", 1), ("A", "mean", 1), ("A", "total", 1)}

# only ranked results are kept: 12 slow laps-only sessions -> the worst ones are dropped
for i in range(12):
    R.add([res("S%d" % i, 1, 9000 + i, 9000 + i, 0, 0)])
assert len(R.ranking("best")) == TOP and all(r["name"] != "S11" for r in R.items), [r["name"] for r in R.items]
# a 10-lap race has its own total table
R.add([res("D", 10, 4200, 4500, 45000, 10)])
assert R.total_groups() == [5, 10] and R.ranking("total", 10)[0]["name"] == "D"
# delete, then save / load
uid = R.ranking("best")[0]["uid"]
assert R.delete(uid) and not R.delete(uid)
R2 = Records()
R2.load(R.dump())
assert R2.items == R.items and R2.next_uid == R.next_uid
print("records    : OK", len(R.items), "kept")
