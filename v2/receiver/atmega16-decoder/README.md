# Gate IR decoder (ATmega16): original firmware

Copy of the ATmega16 dump of a working gate (old repository,
`Software/Receiver/irda_robi/atmega16_dump/`). This is the IR decoder (Robitronic protocol)
that sends the passes to the ATmega328PB over its UART (38400 baud).

| File | Content |
|---|---|
| `flash.hex` | firmware (partly in the bootloader area, 0x3F00) |
| `eeprom.hex` | configuration (a few bytes) |
| `avrdude-info` | original readout, with the fuses |

Original fuses: **lfuse 0x1F** (external 16 MHz resonator, BOD 4 V), **hfuse 0xC8**
(boot from the bootloader area: **required**, the code lives there). The ATmega16 has no efuse.

Programmer on **P2** (ISP of the ATmega16), not P1. Expected signature: `1E 94 03`.

```
AD=$(ls -d ~/.platformio/packages/tool-avrdude* | head -1)
$AD/avrdude -C $AD/avrdude.conf -c usbtiny -p m16 \
  -U flash:w:flash.hex:i -U eeprom:w:eeprom.hex:i -U lfuse:w:0x1F:m -U hfuse:w:0xC8:m
```
