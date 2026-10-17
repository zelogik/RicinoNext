# RicinoNext: controller (Python / MicroPython) · v1.0.0

Lap counter for Mini-Z (Robitronic-style IR emitters). The same code runs on a PC
(simulator) and on an ESP32 (MicroPython). It is derived from the original firmware
`Software/Controller/RicinoNext-controller.ino`.

**Installing on the hardware: see [INSTALL.md](INSTALL.md).** Gate firmware: [../receiver](../receiver/README.md).

## Features

- **Lap**, **time** or **free practice** race, **solo** mode, **standing** or **flying** start,
  start countdown adjustable from 5 to 30 s (on screen: digits or **F1 lights**, chosen per device;
  optional F1 lights on a WS2812 strip), start beeps
  and a beep at every pass on the line (higher pitch on a personal best).
- Full-screen standings: gap to the leader, current lap by sector (gates), best lap in purple
  and personal best in green, ▲▼ trend compared with the previous lap, **animated overtakes**,
  projector mode (`F` or `#display`).
- **Results**: podium, lap-time chart, lap-by-lap details. The history is kept by
  the controller, so a page reloaded during a race gets it back. Shown at the finish, then reopened with
  "📊 Last results" on the race page.
- **Garage** (up to 50 cars): name and IR code of each car, **code reading** (📡) by passing the
  car over the line, car record. **Drivers** (name, color) pick their car.
- **🏆 Records** (tab): top 10 best lap, best mean, best total time per lap count,
  kept in permanent memory, with driver, car and date; a time can be deleted.
- Race settings and drivers **kept** across restarts.
- Light / dark / auto theme.
- **English or French** interface: EN / FR button in the header, per device (default: the browser's language;
  `?lang=en` / `?lang=fr` in the address forces one, handy for a projector).
- ESP32: Wi-Fi setup and hostname (`<name>.local`), fallback access point with captive portal,
  **OTA** updates (files and MicroPython firmware), demo mode, recovery mode,
  **physical Start/Stop button** and **start lights** on a WS2812 strip.

## Counting rules

- **Debounce**: a pass only counts after **3 s** (adjustable 1-10 s) **without any detection** of the same
  car at the same gate. A car stopped on the line or a double detection does not make a lap.
- **Gate order**: 1 → 2 → … → 1. A driver's clock starts at their first pass on the line;
  sectors are only measured between two consecutive gates.
- **Skipped gate**: **tolerated** (probably a missed detection) if the driver made no gate error in their
  **last N passes** (N = 10, adjustable, 0 = never); otherwise **the current lap is not counted** and the
  next one starts from the line. Both cases are announced and counted (⚠ tolerated, 🚫 laps not counted).

## Layout

```
controller/
  main.py            entry point: detects PC or ESP32 (+ recovery mode on ESP32)
  ricino/core.py     race: state machine, lap/sector timing, I2C packets, JSON protocol
  ricino/web.py      HTTP server (static files, .gz preferred) + WebSocket /ws
  ricino/sim.py      simulated gates and cars (PC, and demo mode on ESP32)
  ricino/pc.py       PC platform: command-line options, clock, settings.json
  ricino/esp32.py    ESP32 platform: I2C or RS485 bus, Wi-Fi, hostname, system API, OTA
  ricino/lights.py   F1 start lights on a WS2812 strip (ESP32)
  ricino/button.py   Start/Stop push button or switch (ESP32)
  ricino/rs485.py    RS485 frames controller <-> gates (reference for the receiver firmware)
  data/index.html    race page (standings, results, garage, records, race settings)
  data/system.html   system page: Wi-Fi, hostname, hardware, updates (ESP32)
  tools/deploy.sh    USB install on the ESP32 (compiles the modules to .mpy)
  test_controller.py end-to-end test of the simulator (raw WebSocket)
  test_esp32.py      ESP32 code run against fake MicroPython modules
  test_rules.py      counting rules (debounce, gate order, tolerance)
  test_records.py    records (top 10)
  test_i18n.py       every text of the web pages has its French translation
```

On the ESP32 the modules are installed **precompiled to `.mpy`** (see INSTALL.md): otherwise compiling them
at boot leaves the Wi-Fi without memory.

`core.py`, `web.py` and `sim.py` only use what exists in both CPython and
MicroPython (no `:=`, no f-strings, no `os.path`). The structure of `core.py` follows
the `.ino` (same names, same `loop()`) for a possible C++ port. Fixes compared with
the original firmware are marked `FIX:`.

## On a PC

```
python3 main.py                        # http://localhost:8080/
python3 main.py --speed 5 --laptime 4  # faster race
python3 main.py --settings ""          # do not keep the settings (settings.json)
python3 test_controller.py
python3 test_esp32.py
python3 test_rules.py                  # counting rules (debounce, gate order, tolerance)
python3 test_records.py                # records (top 10)
python3 test_i18n.py                   # translations of the web pages
```

No dependencies (standard library only).

## Translations

The pages and the code are written in English; each page holds its French version in a `FR` dictionary
(English text → French text), next to its script.

- Static text: mark the element with `data-t` (its content, markup included) or `data-t="title"` /
  `data-t="placeholder"` (an attribute).
- Text built by the script: `t("Lap {n}", {n})`, and `tn(n, "{n} lap", "{n} laps")` for plurals.
- Texts sent by the controller (race messages, API errors) stay in English and are translated by the page.

`test_i18n.py` lists every text that has no French version yet.

## WebSocket protocol (`/ws`)

The `conf` values sent by the client are **strings** (the firmware reads them with `atoi`),
except `names` (array of objects).

| Message | Fields |
|---|---|
| `conf` ↔ | laps, time (ms), players, gates, light, reset, style (0 laps, 1 time, 2 free), solo, warmup (s), start (0 standing, 1 flying), read (tag reading 0/1), state, names[{id, name, color}]; on connection also laps_m, time_m (min), players_m, gates_m |
| `race` → | state, lap, time, message?, countdown? (ms, during the countdown) |
| `live` → | rank, id, lap, best, last, mean, total (own time), cross (time of the last pass on the line), sector (last gate passed), gate[] (sector times) |
| `history` → | {id: [lap times]} on connection, during or after a race |
| `tag` → | id, gate, hit, strength (tag reading mode) |
| `debug` → | time, memory, … |

## Link to the gates: I2C or RS485

Selected on the system page. In both cases each gate provides the same 13-byte packet
(`0x82` status, `0x83` timestamp, `0x84` emitter pass); only the transport changes.

- **I2C**: SDA 21, SCL 22, 100 kHz (SoftI2C option). Short cable.
- **RS485**: UART2, TX 26, RX 25, 57600 baud, 8N1, auto-direction module (HW-519).
  Long cable (tens of meters), A+/B- twisted pair. No 120 Ω termination on a short cable
  (see [../README.md](../README.md) §3).

RS485 frames (`ricino/rs485.py`), CRC-8 Dallas/Maxim (the same as in the `0x84` packet):

```
request  controller -> gate : A5 | address | command | crc8(address, command)
reply    gate -> controller : 5A | address | 0D | packet[13] | crc8(address, 0D, packet)
commands: 01 poll, 02 repeat the last reply, 8A start, 8F stop; address 0 = all gates, no reply
```

The controller polls a gate every 20 ms (waiting 10 ms at most for the reply). If a reply
is lost or corrupted, the next poll asks for it again (`02`), so no pass is
lost, and a duplicate is discarded. The start
goes out as a single frame to address 0: all gates start their clock at the same instant.

## System API (ESP32, `data/system.html`)

`GET /api/system`, `GET /api/scan`, `POST /api/config`, `POST /api/upload?path=…`,
`POST /api/firmware`, `POST /api/reboot`. Write routes require the `X-Password` header
if an administration password is set. On a PC, `/api/system` answers `platform: "pc"` and
write routes answer 403.

## Files on the ESP32

| File | Content |
|---|---|
| `/config.json` | Wi-Fi, hostname, passwords, bus (I2C / RS485), demo mode |
| `/race.json` | race settings, drivers and garage (saved 3 s after the last change) |
| `/records.json` | records (top 10) |
| `/error.log` | last boot error (recovery mode) |
