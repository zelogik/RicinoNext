"""PC platform (CPython): simulated gates and cars, served on localhost.

Usage: python3 main.py [--port 8080] [--laptime 6] [--speed 1]
"""
import argparse, asyncio, json, os, platform, sys, time
from ricino import core, sim, web

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(BASE, "data")
SETTINGS = os.path.join(BASE, "settings.json")      # race settings + drivers, kept between runs
RECORDS = "records.json"                            # arcade top 10, next to the settings file
_t0 = time.monotonic()
_last_us = [0]


def run():
    ap = argparse.ArgumentParser(description="RicinoNext controller, PC simulator")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--laptime", type=float, default=6.0, help="average simulated lap time in seconds")
    ap.add_argument("--speed", type=float, default=1.0, help="clock multiplier (2 = twice as fast)")
    ap.add_argument("--settings", default=SETTINGS, help="settings file ('' = do not keep settings)")
    a = ap.parse_args()

    def load():
        with open(a.settings) as f:
            return json.load(f)

    def save(d):
        with open(a.settings + ".new", "w") as f:
            json.dump(d, f, indent=1)
        os.replace(a.settings + ".new", a.settings)

    rec_path = os.path.join(os.path.dirname(a.settings) or ".", RECORDS) if a.settings else ""

    def load_rec():
        with open(rec_path) as f:
            return json.load(f)

    def save_rec(d):
        with open(rec_path + ".new", "w") as f:
            json.dump(d, f, indent=1)
        os.replace(rec_path + ".new", rec_path)

    def millis():
        return int((time.monotonic() - _t0) * 1000 * a.speed)

    def elapsed_us():
        now = int(time.monotonic() * 1_000_000)
        dt, _last_us[0] = now - _last_us[0], now
        return dt if dt < 10_000_000 else 0

    async def system(req, w):
        web.reply_json(w, dict(platform="pc", version=core.VERSION, hostname=platform.node(),
                               python=sys.version.split()[0], simulate=True, wifi=False, ota=False))

    async def esp_only(req, w):
        web.reply_json(w, {"error": "only available on the ESP32"}, "403 Forbidden")

    async def bus_info(req, w):
        web.reply_json(w, {"bus": "simulated"})

    routes = {("GET", "/api/system"): system, ("GET", "/api/bus"): bus_info}
    for path in ("/api/config", "/api/upload", "/api/firmware", "/api/reboot"):
        routes[("POST", path)] = esp_only
    routes[("GET", "/api/scan")] = esp_only

    def stats():                                          # the simulator's own process
        d = dict(cpu=web.cpu_load, uptime=int(time.monotonic() - _t0))
        try:
            with open("/proc/self/statm") as f:
                d["rss"] = int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE")
        except (OSError, ValueError, AttributeError):
            pass
        return d

    core.platform_stats = stats
    server = web.Server(DATA_DIR, routes)
    bus = sim.install(a.laptime)
    core.setup(bus, millis, elapsed_us, lambda: "n/a", server.broadcast, server.send_to, a.speed,
               load if a.settings and os.path.exists(a.settings) else None, save if a.settings else None,
               load_rec if rec_path and os.path.exists(rec_path) else None, save_rec if rec_path else None)

    async def main():
        await server.start(a.port)
        print("RicinoNext %s (PC simulator) on http://localhost:%d/" % (core.VERSION, a.port))
        await web.run_loop(0.001)

    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
