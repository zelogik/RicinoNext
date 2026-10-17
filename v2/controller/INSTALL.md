# Installing on the hardware: checklist

Steps marked **⚠ check** are the ones most likely to need an adjustment on your setup.
The names below are those of the English interface (the EN / FR button in the header switches the language).

## 1. Hardware

- [ ] **ESP32** with 4 MB of flash (e.g. ESP32 DevKit v1 / WROOM-32). The OTA variant of MicroPython needs 4 MB.
- [ ] A **data** USB cable (not a charge-only one).
- [ ] RicinoNext receivers (ATmega16 + ATmega328PB), working ATmega16 decoder.
- [ ] **USBtinyISP** programmer (or USBasp, or an Arduino running "ArduinoISP") on header **P1** (ISP of the ATmega328PB, not P2),
      wired **signal by signal by name** (the order of P1 differs from the standard ISP header, see the receiver README).
- [ ] **Fuses of each ATmega328PB** (once per chip, MANDATORY: from the factory they run at 1 MHz):
      `cd receiver && uvx platformio run -t fuses`, then check that lfuse = `0xf7`.
- [ ] **Receiver firmware** on each gate: `cd receiver && uvx platformio run -t upload` (see [../receiver/README.md](../receiver/README.md)).
- [ ] **Address of each gate with solder jumpers SJ3-SJ5** (no need to recompile):
  - [ ] no jumper = 21 = **finish line** (gate 1)
  - [ ] SJ3 = 22 = gate 2, SJ4 = 23 = gate 3 (split times, in the order they are passed)
  - [ ] ⚠ check that the jumpers go to ground (otherwise the address stays 21)
- [ ] Test each gate alone on the bench: the LED blinks slowly (1 Hz) as soon as the controller polls it.

## 2a. RS485 wiring (recommended for a real track)

**HW-519** modules (auto-direction MAX485: VCC / TXD / RXD / GND on the board side, A+ / B- / GND on the bus side).

- [ ] ESP32 side: module powered with **3.3 V** (module VCC on the ESP32 3V3, GND on GND).
      Check with a multimeter that the module's RXD pin does not go above 3.3 V.
- [ ] ESP32 **GPIO 26 (TX) → TXD** of the module, **GPIO 25 (RX) → RXD**. If no gate answers,
      **swap TXD/RXD**: depending on the maker, the label is seen from the board or from the module, and swapping is harmless.
- [ ] Bus: **A+ to A+**, **B- to B-** on all modules, on a twisted pair (e.g. one pair of an RJ45 cable),
      plus a **common ground**. Gates are **daisy-chained** (not wired as a star).
- [ ] **Termination**: on a short cable (a few meters), **no 120 Ω resistor** with these auto-direction
      modules (it crushes the bias of the "1" bits: bytes corrupted into `00`). Remove the ones soldered
      on the modules. Long cable: 120 Ω at both ends **and** a strong bias at one end
      (≈ 470 Ω from A to +5 V, 470 Ω from B to ground).
- [ ] HW-519 labels **straight**: board TX → module **TXD**, RX → **RXD** (checked on both sides).
- [ ] Gate side, everything on connector **J1**: HW-519 powered with **5 V** (J1 1-2), RXD/TXD on **J1 6 (PB0) / J1 5 (PB1)**, receiver firmware `rs485`.
- [ ] System page > Hardware > Link: **RS485**, TX 26, RX 25, **57600**, **Save and restart**.

## 2b. I2C wiring (gates close together, short cable) ⚠ check

- [ ] SDA → **GPIO 21**, SCL → **GPIO 22** (can be changed later on the system page), **common ground**.
- [ ] **Logic levels**: the ESP32 runs at **3.3 V** and does not tolerate 5 V. If the ATmega328PBs run at 5 V,
      add a **bidirectional I2C level shifter** (BSS138 module). Pull-ups to 5 V directly on the ESP32
      damage it, and pull-ups to 3.3 V may be too low for a 5 V ATmega.
- [ ] **4.7 kΩ pull-ups** on SDA and SCL, on the 3.3 V side and on the 5 V side (often already on the module).
- [ ] Cable length: I2C dislikes long distances. Beyond 1 to 2 m, lower the frequency
      (50 kHz) or use RS485.

## 2c. Start lights (WS2812 strip, optional)

- [ ] Strip data on **GPIO 27** (can be changed), through a 330 Ω series resistor.
- [ ] Strip powered with **its own 5 V** (8 LEDs ≈ 0.5 A at most), **grounds connected** to the ESP32.
- [ ] ⚠ 5 V WS2812s usually read the ESP32's 3.3 V fine on a short wire. If the colors are
      erratic: shorten the wire, or add a level shifter (74AHCT125) on the data line.
- [ ] System page > Hardware: tick **Start lights**, GPIO, number of LEDs, brightness, then **Save and restart**.

## 2d. Start/Stop button or switch (optional)

- [ ] Contact between **GPIO 33** (can be changed) and **ground**. No resistor: the internal pull-up is used.
      Do not use GPIO 34 to 39 (no internal pull-up).
- [ ] To try without wiring anything: GPIO **0** = **BOOT** button of the development board.
- [ ] System page > Hardware: tick **Physical Start/Stop button**, pick the GPIO and the type:
  - **Push button**: press = start, **1 s long press = stop** (an accidental press does not stop the race);
  - **Switch**: ON = start, OFF = stop (a switch already ON at boot starts nothing).
- [ ] Long cable to the button: wire twisted with ground, and possibly a 100 nF capacitor between the GPIO and ground.

## 3. Tools on the PC

- [ ] esptool and mpremote **from PyPI**, through `uvx` (nothing to install): `uvx esptool version`, `uvx mpremote version`.
      ⚠ Do not use the Debian `esptool` package (4.7.0+dfsg): it lacks the classic ESP32 flasher stub
      (`stub_flasher_32.json` not found).
- [ ] Serial port access: `sudo usermod -aG dialout $USER`, then log in again.
- [ ] Find the port: plug in the ESP32, then `ls /dev/ttyUSB* /dev/ttyACM*`.

## 4. Flash MicroPython (once, over USB)

- [ ] On https://micropython.org/download/ESP32_GENERIC/ take the **OTA variant** ("Support for OTA"),
      latest stable release. Download:
  - [ ] the full `.bin`, for USB;
  - [ ] the `.app-bin`, for future OTA updates.
- [ ] Identify the board (read only): `uvx esptool --port /dev/ttyUSB0 flash-id` → ESP32, 4 MB minimum.
- [ ] `uvx esptool --chip esp32 --port /dev/ttyUSB0 erase-flash`
- [ ] `uvx esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x1000 ESP32_GENERIC-OTA-xxxx.bin`
- [ ] Check: `uvx mpremote connect /dev/ttyUSB0 exec "import sys; print(sys.implementation)"`

## 5. Install RicinoNext

- [ ] `cd controller && tools/deploy.sh /dev/ttyUSB0` (compiles the modules to `.mpy`, see the last section).
- [ ] Follow the boot: `uvx mpremote connect /dev/ttyUSB0 repl`, then Ctrl-D (soft reset).
      Expected: `Wi-Fi: access point RicinoNext-XXXX`, the gates found, `RicinoNext 1.0.0 ready`.
- [ ] If a Python error shows up: note the traceback (it is also saved in `/error.log`).
      Recovery mode then starts by itself (access point `RicinoNext-recovery`, file upload page).

## 6. Configuration

- [ ] On the phone, join the **RicinoNext-XXXX** Wi-Fi. The system page opens by itself,
      otherwise go to http://192.168.4.1/system.html.
- [ ] Wi-Fi: **Scan**, pick the network, enter the password, choose the **hostname**, then **Save and restart**.
- [ ] Join that network, open `http://<name>.local/`.
      If `.local` does not work (some Android phones), use the IP shown in the REPL or by the router.
- [ ] Set an **administration password** (Hardware), and a password for the fallback access point.

## 7. Tests on the board

- [ ] **Demo mode** (Hardware > Demo mode): run a simulated race, check the display and its smoothness.
      Look at the **free memory** in Settings > Debug: at least 30 KB during a race.
- [ ] Turn demo mode off, then check **Gates detected**: 21 (and 22, 23).
  - [ ] ⚠ Missing gate or intermittent detection: see [../README.md](../README.md) §8 (Troubleshooting) and
        System > Link diagnostics.
- [ ] **Garage** > 📡: pass each car over the line, name it (saved automatically).
- [ ] Settings > Drivers: give each driver a car.
- [ ] Restart the ESP32: drivers, garage and settings are still there (`/race.json`).
- [ ] Real 3-lap race: compare a few lap times with a stopwatch.
- [ ] ⚠ With several gates: check that the sector times (S1, S2, S3) are consistent.
- [ ] Reload the page during a race: the lap history comes back.
- [ ] Projector: `http://<name>.local/#display`, click once to enable the beeps.
- [ ] Lights: start a race, check orange → 5 red lights → green at GO → checkered at the finish.
- [ ] Button: press = start (pages switch to the race view), long press = stop.

## 8. Test OTA (before you need it)

- [ ] System > Update: send `data/index.html` again (compressed automatically), then reload the page.
- [ ] Send a module again as `.mpy` (e.g. `core.mpy` to `/ricino/`), then restart.
- [ ] Firmware: send the `.app-bin` of the same version, and check that the **active partition** changed (ota_0 ↔ ota_1).
- [ ] Optional: send a deliberately broken module, check that **recovery mode** lets you fix it.

## Precompiled modules (.mpy): mandatory

The code is sent to the board **precompiled to `.mpy`** (`tools/deploy.sh` does it with `uvx mpy-cross`).
Compiling the `.py` files on the board takes so much memory that the Wi-Fi no longer finds its own at boot
("OSError: WiFi Out of Memory", the board stops answering). `mpy-cross` must have the **same version as the
board's MicroPython** (1.29 here: `uvx --from "mpy-cross==1.29.*" mpy-cross -o core.mpy ricino/core.py`).

- [ ] OTA update of a module: send the **`.mpy`** (the system page then removes the old `.py`).
- [ ] If the board stops answering after an update: `uvx mpremote connect /dev/ttyUSB0 repl` shows the error;
      `tools/deploy.sh /dev/ttyUSB0` reinstalls everything over USB.
