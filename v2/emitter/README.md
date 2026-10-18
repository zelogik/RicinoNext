# RicinoNext: emitter (IR transponder) · firmware v2

Firmware of the small board each car carries: an ATtiny24 (or 44/84, or 25/45/85) flashes a
Robitronic-compatible IR code that the gates decode. It replaces the first firmware
([`Software/Emitter/irda_robi`](../../Software/Emitter/irda_robi)) and is **compatible with it**: same frame, same
EEPROM layout, so a reflashed car keeps its ID (and its place in the garage).

Hardware: [`Hardware/Emitter/irda_robi`](../../Hardware/Emitter/irda_robi) (ATtiny24A, IR LED SFH 4259 switched by an
SS8050 transistor on PB2, heartbeat LED on PA7, 6-pin 1.27 mm ISP header).

## What changed from the first firmware

| | First firmware | v2 |
|---|---|---|
| Bit (slot) period | ≈ 7.1 µs with ATTinyCore + LTO, ≈ 9 µs without | **10 µs**, exact to the cycle |
| IR pulse | ≈ 1.1 µs | **2 µs** (like the original transponder: almost twice the light) |
| Byte to byte | ≈ 80 µs | **103.5 µs** (original: 13-14 µs between the last slot and the next start) |
| Frames lost to an interrupt | ≈ 1 in 7 (Arduino millis timer, 12 µs) | **none** (no interrupt at all) |
| Delay between frames | ≈ 2.5-15 ms | **0.9-4.35 ms**, like the original: ≈ 3× more frames per pass |
| Random delays | same sequence on every car | each car its own (seeded with its ID) |
| New ID | sum of RAM bytes: 16 bits at most, top byte always 0 | 24 random bits (watchdog vs CPU clock jitter) |
| "New ID" key | never worked (`== 1` on a 0/64 value, pin floating) | MOSI to GND at power-up, internal pull-up |
| Damaged EEPROM | sent a wrong CRC forever | CRC / parity checked and repaired at start |
| Brown-out | off (EEPROM at risk with bouncy RC plugs) | BOD 2.7 V (fuse) |
| Flash | 1612 bytes (Arduino core) | ≈ 1100 bytes (plain avr-gcc) |

The frame itself (bit order, inversion, parity, CRC-8 0x07) is unchanged: the test compares both firmwares on
2.6 million IDs, and the CRC with frames captured from real Robitronic transponders.

## Frame (unchanged)

4 bytes: the 24-bit ID, least significant byte first, then CRC-8 (polynomial 0x07) of these 3 bytes. Each
byte is 10 slots of 10 µs: a start pulse, the 8 bits most significant first and inverted (pulse = 0), then a
parity slot (pulse when the byte has an even number of 1 bits). A pulse is a 2 µs IR flash at the start of
its slot; bytes start 103.5 µs apart; a frame lasts 0.41 ms.

## Build and flash

PlatformIO, ISP programmer (USBtinyISP by default) on the emitter header
(1 GND · 2 VCC · 3 MOSI · 4 MISO · 5 SCK · 6 RESET):

```bash
cd v2/emitter
uvx platformio run                 # build for the ATtiny24 (-e attiny44 / attiny84 / attiny85 otherwise)
uvx platformio run -t fuses        # FIRST, once per chip: 8 MHz internal, BOD 2.7 V, EESAVE
uvx platformio run -t upload       # flash (-e usbasp for a USBasp)
```

Fuses: **lfuse 0xE2** (internal 8 MHz, no /8), **hfuse 0xD5** (BOD 2.7 V, **EESAVE**), efuse 0xFF.

⚠ **Keep the car's ID:** flashing erases the chip, and with the factory fuses the EEPROM goes with it (the car then
gets a new ID). Writing the fuses first sets EESAVE, so the upload keeps the EEPROM. To be safe, save it before:

```bash
AD=$(ls -d ~/.platformio/packages/tool-avrdude* | head -1)
$AD/avrdude -C $AD/avrdude.conf -c usbtiny -p t24 -U eeprom:r:car-ID.hex:i     # backup
$AD/avrdude -C $AD/avrdude.conf -c usbtiny -p t24 -U eeprom:w:car-ID.hex:i     # restore if needed
```

A fixed ID instead of a random one: `build_flags = -DTX_ID=123456` in `platformio.ini`.

## ID

- **First start** (blank EEPROM): a 24-bit random ID is drawn (about 1 s) and saved.
- **New ID:** hold MOSI (header pin 3) to GND (pin 1) while powering up. Then read the car again in the
  Garage (📡).
- **Every start:** the CRC and parity stored next to the ID are checked; if they are damaged they are rewritten
  (the ID is kept).

## LED

| Pattern | Meaning |
|---|---|
| heartbeat (two short flashes, then a pause) | running, sending its ID |
| 12 quick blinks at power-up, then heartbeat | a new ID was just created, or the EEPROM was repaired: read the car again in the Garage |

## Tests (PC, no board)

```bash
python3 emitter/test/test_emitter.py
```

- **protocol:** `src/protocol.h` against the first firmware's code (2.6 million IDs) and real transponder CRCs.
- **simulator:** the built firmware runs in simavr (`pio pkg install -g -t platformio/tool-simavr`). The IR pin is
  measured to the CPU cycle and every frame is decoded, for these cases: an existing car (ID kept), the new-ID
  key, a blank chip, and a damaged EEPROM. Run `pio run -e attiny24 -e attiny85` first. In the simulator the
  oscillators have no jitter, so the "random" ID is always the same there; on a real chip it is not.

On a real board, a logic analyser on the transistor's base (or the LED) should show exactly the same thing:
2 µs pulses on a 10 µs grid, bytes 103.5 µs apart, frames 0.9-4.35 ms apart. The internal oscillator is
accurate to a few %; the gates' decoder accepted even the ≈ 7 µs bits of the first firmware, so this is
comfortable.
