"""Arcade-style records, kept in flash (/records.json on the ESP32, records.json on the PC).

Three top-10 tables, computed from the stored race results:
  best   best single lap
  mean   best average lap, sessions of at least MIN_LAPS_MEAN laps
  total  best total time for a race of N laps (only drivers who completed the N laps of a LAPS race),
         one table per N
Only results that rank in at least one table are kept, so the file stays small.
CPython / MicroPython common subset.
"""
TOP = 10
MIN_LAPS_MEAN = 3
KINDS = ("best", "mean", "total")


class Records:
    def __init__(self):
        self.items = []         # dict(uid, at, id, name, color, laps, best, mean, total, race_laps)
        self.next_uid = 1

    def load(self, data):
        self.items = [r for r in data.get("items", []) if isinstance(r, dict) and "uid" in r]
        self.next_uid = max([data.get("next", 1)] + [r["uid"] + 1 for r in self.items])

    def dump(self):
        return {"items": self.items, "next": self.next_uid}

    def ranking(self, kind, race_laps=None):
        if kind == "best":
            pool = [r for r in self.items if r["best"] > 0]
        elif kind == "mean":
            pool = [r for r in self.items if r["laps"] >= MIN_LAPS_MEAN and r["mean"] > 0]
        else:
            pool = [r for r in self.items if r["race_laps"] and r["laps"] >= r["race_laps"] and r["total"] > 0
                    and (race_laps is None or r["race_laps"] == race_laps)]
        pool.sort(key=lambda r: (r[kind], r["uid"]))
        return pool[:TOP]

    def total_groups(self):
        groups = []
        for r in self.items:
            if r["race_laps"] and r["laps"] >= r["race_laps"] and r["race_laps"] not in groups:
                groups.append(r["race_laps"])
        groups.sort()
        return groups

    def _prune(self):
        keep = set()
        for kind in ("best", "mean"):
            for r in self.ranking(kind):
                keep.add(r["uid"])
        for n in self.total_groups():
            for r in self.ranking("total", n):
                keep.add(r["uid"])
        self.items = [r for r in self.items if r["uid"] in keep]

    def add(self, results):
        """results: list of dicts (at, id, name, color, laps, best, mean, total, race_laps).
        Returns the new records: list of dict(uid, name, kind, rank, value, race_laps)."""
        uids = []
        for r in results:
            r = dict(r)
            r["uid"] = self.next_uid
            self.next_uid += 1
            self.items.append(r)
            uids.append(r["uid"])
        self._prune()
        news = []
        for uid in uids:
            for kind in KINDS:
                table = self.ranking(kind, self._laps_of(uid)) if kind == "total" else self.ranking(kind)
                for pos, r in enumerate(table):
                    if r["uid"] == uid:
                        news.append(dict(uid=uid, name=r["name"], kind=kind, rank=pos + 1, value=r[kind],
                                         race_laps=r["race_laps"]))
        return news

    def _laps_of(self, uid):
        for r in self.items:
            if r["uid"] == uid:
                return r["race_laps"]
        return None

    def delete(self, uid):
        n = len(self.items)
        self.items = [r for r in self.items if r["uid"] != uid]
        return len(self.items) != n
