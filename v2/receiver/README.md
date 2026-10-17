# RicinoNext: receiver (gate) · firmware v3

One firmware for all gates. It runs on the **ATmega328PB** of the receiver board
("IRDA_I2C" PCB 2021-09), between the IR decoder (ATmega16, Robitronic protocol, UART 38400)
and the ESP32 controller. Lights, sound and start lights are handled by the controller, not by the gates.

## Link to the controller

| Build option | Link | 328PB pins |
|---|---|---|
| `LINK_RS485=1` (default, env `rs485`) | RS485 57600 8N1, auto-direction module (HW-519) | **D8 = PB0 = RX** (J1 pin 6), **D9 = PB1 = TX** (J1 pin 5), imposed by AltSoftSerial (Timer1) |
| `LINK_RS485=0` (env `i2c`) | I2C slave, as before | SDA/SCL of connector J1 |

RS485 wiring of a gate, **everything on connector J1**: HW-519 module powered with **5 V** (J1 1-2, check the
VCC/GND order with a multimeter), its **RXD/TXD** on **J1 6 (PB0) / J1 5 (PB1)** (if the gate does not answer,
swap the two wires), **A+/B-** on the bus, common ground.

Board connectors (read from the schematic `Schematic_IRDA_I2C_2021-09-16.pdf`):

| Connector | Pins |
|---|---|
| **J1** (main) | 1 GND, 2 VCC, 3 SCL (PC5), 4 SDA (PC4), 5 PB1 (D9), 6 PB0 (D8) |
| **P1** (1.27 mm) | **ISP of the ATmega328PB**: 1 GND, 2 VCC, 3 MOSI, 4 MISO, 5 SCK, 6 RESET |
| **P2** (1.27 mm) | ISP of the ATmega16 (decoder), same pinout: see [atmega16-decoder](atmega16-decoder/README.md) |

## Gate address

Solder jumpers **SJ3 / SJ4 / SJ5** (PD5 / PD6 / PD7), read with pull-ups: closed = tied to ground.

| SJ5 | SJ4 | SJ3 | Address | Role |
|---|---|---|---|---|
| - | - | - | 21 | **finish line** (gate 1) |
| - | - | ● | 22 | gate 2 |
| - | ● | - | 23 | gate 3 |
| - | ● | ● | 24 | … up to 28 |

⚠ Check on the board that the jumpers tie PD5-PD7 to **ground**. If they go to VCC,
the address stays 21 whatever the setting: the reading in `readAddress()` would then have to be inverted.

## Build and flash

The board is drawn for an ATmega328P, but it carries an **ATmega328PB** (signature `1E 95 16`): the project
targets the 328PB. AltSoftSerial is included in `lib/`, with the 328PB added (the published version does not know it).
For a board with a genuine 328P ("expected signature … 1E 95 0F"), set `board = ATmega328P` in `platformio.ini`.

**ISP** programmer on **P1**: **USBtinyISP** by default (or USBasp, or an Arduino running "ArduinoISP"):

```
cd receiver
uvx platformio run                             # build (RS485); -e i2c for the old link
uvx platformio run -t fuses                    # MANDATORY the first time: external 16 MHz (otherwise 1 MHz)
uvx platformio run -t upload                   # flash with the USBtinyISP
uvx platformio run -e rs485-usbasp -t upload   # or with a USBasp
uvx platformio run -e rs485-arduinoisp -t upload --upload-port /dev/ttyACM0   # or an Arduino as ISP
```

⚠ **Fuses**: the boards' ATmega328PBs leave the factory with lfuse = **0x62** (internal oscillator
8 MHz ÷ 8 = **1 MHz**). Everything then runs 16 times too slowly: 57600 baud becomes 3600, the gate
understands nothing and its replies arrive as `00` bytes. lfuse must be **0xF7** (16 MHz resonator; `0xFF` works too):
`uvx platformio run -t fuses`, or directly `avrdude -c usbtiny -p m328pb -U lfuse:w:0xF7:m`.
Check: `avrdude -c usbtiny -p m328pb -U lfuse:r:-:h` must print `0xf7`.

Programmer wiring: its standard ISP header (2.54 mm) does **not have the same order** as P1, connect
it wire by wire **by signal name**:

| Signal | Standard 6-pin ISP | Board P1 |
|---|---|---|
| MISO | 1 | 4 |
| VCC | 2 | 2 |
| SCK | 3 | 5 |
| MOSI | 4 | 3 |
| RESET | 5 | 6 |
| GND | 6 | 1 |

"initialization failed" or "target doesn't answer": check the wires and the power, then slow down
the programming clock: `uvx platformio run -t upload --upload-flags="-e -B 32"`.

⚠ Connect the programmer to **P1** (ATmega328PB), never to **P2** for this firmware: `-t upload` erases the chip,
and on the ATmega16 that would destroy the Robitronic decoder firmware (to restore it, see
[atmega16-decoder](atmega16-decoder/README.md)). Arduino IDE: open
`RicinoNext-receiver/RicinoNext-receiver.ino`, "ATmega328" board from **MiniCore**, **328PB** variant, external 16 MHz,
**AltSoftSerial** library from the `lib/` folder (copy it into the IDE's libraries folder).

Size: 5.4 KB of flash, 573 bytes of RAM (RS485).

## Status LED

| LED | Meaning |
|---|---|
| N blinks at power-up | gate number (1 = address 21, 2 = 22, …) |
| fast blink (5 Hz) | nothing received for 2 s: check the wiring |
| double flash | bytes or requests heard, but none valid for this gate (TXD/RXD, A/B, baud rate, address) |
| slow blink (1 Hz) | controller present, idle |
| on with short gaps | race running |
| 4 Hz blink | race, but decoder silent or link lost |
| short inversion | a car was just detected |

## Error handling

- The loop never blocks (v1 waited up to 5 ms per byte).
- Decoder frames are checked: plausible length byte, delay between bytes, resynchronization
  (v1 could overflow its 13-byte array on an absurd length).
- Queue of 8 passes: several cars between two polls are no longer lost.
- **Bus cut during a race: the decoder keeps going** (v1 restarted it when the bus came back,
  and all following times were wrong).
- RS485: CRC on every frame, and the reply is resent unchanged if the controller lost it (command `02`).
- Counters reported to the controller in the `0x82` status packet: decoder errors, lost passes
  (queue full), link errors, queue, version, decoder-silent / link-lost flags.
  They show up in the `debug` message and in System > Link diagnostics.

## Protocol

13-byte packets identical over I2C and RS485, RS485 frames and commands: see the header of
`RicinoNext-receiver.ino` and `controller/ricino/rs485.py`. CRC-8 Dallas/Maxim everywhere.

## Bench bring-up tests

- `bringup/`: minimal register-level RS485 test (`T21` → the LED toggles, reply `G21`, 9600 baud);
  env `pintest`: PB1 low 1 s / high 1 s, to check the TX pin with a multimeter.
- `bringup-lib/`: the same test with SoftwareSerial or AltSoftSerial (env `altsoft`).
- ESP32 side: `uvx mpremote connect /dev/ttyUSB0 run receiver/bringup/rs485_bringup_controller.py`
  (round trip) or `.../rs485_listen.py` (listen only; restart the gate while listening: `H21`).

HW-519 wiring checked: labels **straight** (board TX → module TXD, RX → RXD), on both modules.
With these auto-direction modules, **no 120 Ω resistor** on a short cable: it crushes the
bias that holds the bits at 1, and the bytes arrive corrupted. For a long cable with terminations,
add a strong bias at one end (≈ 470 Ω from A to +5 V, 470 Ω from B to ground).

## PC test (no board)

```
python3 receiver/test/test_receiver.py
```

Builds the real `.ino` for the PC (Arduino stub in `test/stub/`) and makes it talk to the
controller's Python code: address, poll, CRC, broadcast start, passes, repeat, decoder
errors, full queue, bus cut during a race, silent decoder, stop.
