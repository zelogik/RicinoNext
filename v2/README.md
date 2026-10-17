# RicinoNext v2: lap counter for Mini-Z

A **free, home-made** lap counter for RC cars (Mini-Z): every car carries a small infrared emitter,
**gates** on the track detect them, and an **ESP32** times the race and shows it on a **web page**
(phone, tablet, PC, projector). Nothing to install for the audience: a browser is enough.

This guide follows the order to use when starting from scratch:

1. [How it works](#1-how-it-works)
2. [Get and build the hardware](#2-get-and-build-the-hardware)
3. [Wiring](#3-wiring)
4. [Program the gates](#4-program-the-gates)
5. [Program the ESP32 controller](#5-program-the-esp32-controller)
6. [Configure](#6-configure)
7. [First race](#7-first-race)
8. [Troubleshooting](#8-troubleshooting)
9. [No hardware: the PC simulator](#9-no-hardware-the-pc-simulator)

---

## 1. How it works

```
   car + IR emitter                                             browsers (Wi-Fi)
      ((( 940 nm )))                                          phone · PC · projector
            │                                                           ▲
            ▼                                                           │ web page + WebSocket
 ┌─────────────── gate ────────────────┐                    ┌───── controller ──────┐
 │ TFDU4100 → ATmega16 → ATmega328PB   │     RS485 bus      │ ESP32 (MicroPython)   │
 │ sensor     decoder    firmware v3   ┼═ HW-519 ══ HW-519 ═┼ timing, web page,     │
 │                                     │   A+/B- + power    │ Wi-Fi, records        │
 └─────────────────────────────────────┘                    └───────────────────────┘
   gate 1 = finish line (21), gates 2, 3… = split times
```

- **Emitter** (one per car): repeats the car's unique code.
- **Gate**: the ATmega16 decodes the infrared (Robitronic protocol) and timestamps every pass;
  the ATmega328PB forwards the passes to the controller over the **RS485 bus**.
- **Controller**: an ESP32 polls the gates, counts the laps, applies the rules (debounce, gate order)
  and serves the web page. It can also drive start lights (LED strip) and a physical Start/Stop button.

Times are measured **by the gates**: a slow or busy controller cannot distort a lap time.

## 2. Get and build the hardware

### Parts

| Item | Quantity | Notes |
|---|---|---|
| **Gate board** "IRDA_I2C" | 1 per gate (at least 1: the finish line) | manufacturing files below |
| **IR emitter** | 1 per car | ATtiny24/44/84 (or 25/45/85) + 940 nm IR LED, see below |
| **ESP32 DevKit** (WROOM-32, 4 MB flash) | 1 | with USB; the external-antenna WROOM-32U has a longer Wi-Fi range |
| **RS485 module HW-519** (auto-direction MAX485) | 1 per gate + 1 for the ESP32 | VCC/TXD/RXD/GND and A+/B-/GND terminals |
| **Network cable** (RJ45, category 5) | from the ESP32 to the last gate | 4 pairs: data + power |
| **5 V power supply** | 1 | ≈ 100 mA per gate + the ESP32; 9-12 V and a regulator per gate for a long cable |
| **ISP programmer** | 1 | USBtinyISP, USBasp, or an Arduino running the "ArduinoISP" sketch |
| **1.27 mm** ISP adapter (or thin wires) | 1 | the gate programming headers are small |
| *Option*: **WS2812** strip (8 LEDs) | 1 | start lights |
| *Option*: **push button** | 1 | Start / Stop without a screen |

### Build the boards

The files are in the repository's `Hardware/` folder (Gerber, bill of materials, pick-and-place, ready for
manufacturing **and assembly** at JLCPCB with LCSC parts):

- **Gate**: [`Hardware/Receiver/irda_robi/PCB files`](../Hardware/Receiver/irda_robi/PCB%20files)
  (schematic `Schematic_IRDA_I2C_2021-09-16.pdf`).
- **Gate extension** (extra sensor for a wide track):
  [`Hardware/Receiver/irda_robi_extension`](../Hardware/Receiver/irda_robi_extension).
- **Emitter** (panel of 6): [`Hardware/Emitter/irda_robi/Schematics`](../Hardware/Emitter/irda_robi/Schematics);
  firmware and pinout: [`Software/Emitter/irda_robi`](../Software/Emitter/irda_robi/README.md). Every emitter
  picks a random code on first power-up and keeps it in EEPROM.

⚠ The bill of materials says **ATmega328P**, but assembled boards often carry an **ATmega328PB**
(signature `1E 95 16`). The v2 firmware targets the **328PB**; see [receiver/README.md](receiver/README.md)
for a genuine 328P.

### Gate board connectors (good to know)

| Name | Role | Pins |
|---|---|---|
| **J1** (2.54 mm, 6 pins) | main connector: power + RS485 | 1 GND · 2 VCC (check with a multimeter) · 3 SCL · 4 SDA · **5 TX (PB1)** · **6 RX (PB0)** |
| **P1** (1.27 mm) | programming the **ATmega328PB** | 1 GND · 2 VCC · 3 MOSI · 4 MISO · 5 SCK · 6 RESET |
| **P2** (1.27 mm) | programming the **ATmega16** (decoder) | same pinout as P1 |
| **SJ3 / SJ4 / SJ5** | solder jumpers: gate **address** | see the table in §4 |

## 3. Wiring

### Overview

```
 5 V SUPPLY
  +5V ──┬────────────────────────────── +V   (pair 2)  ──────────────────┬─────────────► next gate
        │                                                                │
  GND ──┼──┬─────────────────────────── GND  (pair 3)  ──────────────────┼──┬──────────► next gate
        │  │                                                             │  │
 ┌──────┴──┴──── CONTROLLER ─────────────┐                        ┌──────┴──┴──── GATE 21 ────────────────┐
 │  ESP32 DevKit         HW-519          │                        │  HW-519               gate board      │
 │  5V/VIN ◄── +5V                       │                        │  VCC ◄──────────────── J1-2  VCC      │
 │  3V3 ──────────────►  VCC             │                        │  GND ───────────────── J1-1  GND      │
 │  GND ──────────────── GND             │                        │  TXD ◄──────────────── J1-5  TX (PB1) │
 │  GPIO26 (TX) ──────►  TXD             │                        │  RXD ────────────────► J1-6  RX (PB0) │
 │  GPIO25 (RX) ◄─────── RXD             │                        │                                       │
 │                       A+ ═════════════╪══ A+ (pair 1,   ═══════╪ A+ ═══════════════════════════════════╪══► A+ next gate
 │                       B- ═════════════╪══ B-  twisted)  ═══════╪ B- ═══════════════════════════════════╪══► B- next gate
 └───────────────────────────────────────┘                        └───────────────────────────────────────┘
   board TX → module TXD · RX → RXD · all grounds connected · no 120 Ω on a short cable
```

### Controller

```
ESP32 DevKit                         HW-519 #1 (controller side)
  3V3 ──────────────────────────────── VCC        ← 3.3 V: its signals stay at the ESP32 level
  GND ──────────────────────────────── GND
  GPIO 26 (TX) ─────────────────────── TXD
  GPIO 25 (RX) ─────────────────────── RXD
                                       A+ ──┐
                                       B- ──┼──► to the bus cable
                                       GND ─┘
Options:
  GPIO 27 ──[330 Ω]── DIN of the WS2812 strip (strip powered by its own 5 V, grounds connected)
  GPIO 33 ── push button ── GND   (no resistor: internal pull-up)
```

### Each gate

```
Gate board, connector J1             HW-519 (gate side)
  J1-2  VCC 5 V ────────────────────── VCC
  J1-1  GND ────────────────────────── GND
  J1-5  TX  (PB1) ──────────────────── TXD
  J1-6  RX  (PB0) ──────────────────── RXD
                                       A+ / B- / GND ──► bus
```

**HW-519 labels: board TX → module TXD, RX → RXD** (checked on both sides). If a gate does not answer,
swapping these two wires is harmless.

### Bus and power

```
[ESP32]─HW-519 ══════ cable ══════ HW-519─[gate 21] ══════ HW-519─[gate 22] ══════ HW-519─[gate 23]
              (A+, B-, GND, +V)          finish line             sector 2               sector 3
```

| RJ45 cable pair | Use |
|---|---|
| pair 1 (twisted) | **A+ / B-** |
| pair 2 | **+V** (both wires together) |
| pair 3 | **GND** (both wires together): power return and common bus ground |
| pair 4 | spare |

- Gates are **daisy-chained**, not wired as a star. **All grounds are connected.**
- **Short cable (a few meters): no 120 Ω resistor** with these modules. It crushes the level that encodes the
  "1" bits and the messages arrive corrupted. Remove the ones soldered on the modules if needed.
- **Long cable**: 120 Ω at both ends **and** a strong bias at one end (≈ 470 Ω from A to +5 V,
  470 Ω from B to GND); send 9-12 V in the cable with a 5 V regulator in each gate.

## 4. Program the gates

Done **once per board**, ISP programmer connected, board powered.
Tools: `uvx` (from the `uv` package), which runs PlatformIO and avrdude without installing them.

### 4.1 The ATmega16 decoder (header P2): usually nothing to do

Assembled boards already have it. To check or rewrite it, see
[receiver/atmega16-decoder/README.md](receiver/atmega16-decoder/README.md)
(original firmware, fuses **lfuse 0x1F / hfuse 0xC8**).

### 4.2 The ATmega328PB (header P1)

**Fuses: mandatory on a new chip.** From the factory it runs at **1 MHz** instead of 16 MHz: the gate looks
alive but nothing communicates (*the* failure that cost us an evening).

```bash
AD=$(ls -d ~/.platformio/packages/tool-avrdude* | head -1)   # present after a first "uvx platformio run"
$AD/avrdude -C $AD/avrdude.conf -c usbtiny -p m328pb -U lfuse:r:-:h      # 0x62 = factory (1 MHz)
$AD/avrdude -C $AD/avrdude.conf -c usbtiny -p m328pb -U lfuse:w:0xF7:m   # 16 MHz resonator
```

(`0xFF` is fine too: also 16 MHz.) Then the firmware:

```bash
cd v2/receiver
uvx platformio run -t upload          # USBtinyISP; -e rs485-usbasp or -e rs485-arduinoisp otherwise
```

**Gate address** (solder jumpers, to GND):

| SJ5 | SJ4 | SJ3 | Address | Gate | Blinks at power-up |
|---|---|---|---|---|---|
| | | | 21 | **finish line** | 1 |
| | | ● | 22 | gate 2 | 2 |
| | ● | | 23 | gate 3 | 3 |

Details, LED codes and tests: [receiver/README.md](receiver/README.md).

## 5. Program the ESP32 controller

Once over USB; afterwards everything updates **over the air** from the System page.

```bash
# 1. MicroPython, OTA variant (micropython.org/download/ESP32_GENERIC → "Support for OTA")
uvx esptool --port /dev/ttyUSB0 flash-id                                   # ESP32, 4 MB minimum
uvx esptool --chip esp32 --port /dev/ttyUSB0 erase-flash
uvx esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x1000 ESP32_GENERIC-OTA-xxxx.bin

# 2. RicinoNext (the script compiles the modules to .mpy: required, see §8)
cd v2/controller
tools/deploy.sh /dev/ttyUSB0
```

⚠ Use `esptool` from PyPI (`uvx esptool`), not the Debian package (`stub_flasher_32.json` not found).

## 6. Configure

1. With no network configured, the ESP32 opens the **RicinoNext-XXXX** access point: join it with a phone,
   the **System** page opens (otherwise `http://192.168.4.1/system.html`).
2. **Wi-Fi**: pick the network, the password and a **hostname** → the page becomes `http://<name>.local/`.
3. **Hardware**: link **RS485**, TX 26, RX 25, **57600**; "Gates detected" must list the gates (21…).
   Options: start lights (GPIO 27), button (GPIO 33). Set an administration password.
4. Race page, **Settings > Hardware**: **Gates** = number of connected gates.
5. **Garage**: "📡 Read a new car", pass each car in front of the finish line and name it (up to 50 cars).
6. **Settings > Drivers**: name, color and **car** of each driver.

The web interface is in English or French: the **EN / FR** button in the header switches it, per device
(default: the browser's language; `?lang=en` or `?lang=fr` in the address forces one).

## 7. First race

- **Settings > Race**: laps / time / free practice, standing or flying start, countdown, solo mode.
- **Start** (or the physical button): countdown, then GO. The page switches to the race view (big clock).
- A pass only counts after **3 s without detection** of the car at that gate, and the gates must be passed
  **in order** (a missed gate is tolerated now and then, see [controller/README.md](controller/README.md)).
- At the finish: **results** (podium, chart, lap details); the **🏆 Records** tab keeps the top 10.
- Projector: `http://<name>.local/#display` (click once to allow the beeps).

## 8. Troubleshooting

| Symptom | Likely cause | What to do |
|---|---|---|
| Gate: LED stays on for long, nothing communicates | ATmega328PB at **1 MHz** (factory fuses) | §4.2: lfuse = 0xF7 |
| Programming: "expected signature … 1E 95 0F", got `1E 95 16` | the board carries a **328PB** | the project already targets the 328PB; do not force with `-F` |
| Programming: "initialization failed" | programmer badly connected, board not powered | check P1 (328PB) or P2 (ATmega16), orientation, power |
| Replies arrive as `00` bytes | **120 Ω** resistors on a short cable | remove them (§3) |
| No gate answers | TXD/RXD swapped, A/B swapped, missing ground | swap TXD/RXD; test with [receiver/README.md](receiver/README.md) "Bench bring-up tests" |
| ESP32 stops answering, serial port: `WiFi Out of Memory` | `.py` modules compiled at boot | reinstall with `tools/deploy.sh` (`.mpy` modules) |
| `stub_flasher_32.json` not found | Debian's esptool | `uvx esptool` |
| A missed gate cancels laps | repeated missed detections | sensor position, tolerance (Settings > Hardware) |

Built-in diagnostics: **System > Link diagnostics** page (per-gate counters, listen, test), and
**Settings > Debug** (CPU, memory, flash). The ESP32 serial port (`uvx mpremote connect /dev/ttyUSB0 repl`)
shows boot errors.

## 9. No hardware: the PC simulator

The same code runs on a PC with simulated gates and cars, to discover the interface or to develop:

```bash
cd v2/controller
python3 main.py                      # then http://localhost:8080/
python3 main.py --speed 5            # faster race
```

## Going further

| Document | Content |
|---|---|
| [controller/README.md](controller/README.md) | features, counting rules, WebSocket protocol, API, code layout |
| [controller/INSTALL.md](controller/INSTALL.md) | detailed installation checklist of the controller |
| [receiver/README.md](receiver/README.md) | gate firmware, LED codes, RS485 protocol, bench tests |
| [receiver/atmega16-decoder/README.md](receiver/atmega16-decoder/README.md) | original ATmega16 decoder |

Automated tests (PC, no hardware): `controller/test_*.py` and `receiver/test/test_receiver.py`.
