# Décodeur IR des portes (ATmega16) : firmware d'origine

Copie du dump de l'ATmega16 d'une porte qui fonctionnait (ancien dépôt,
`Software/Receiver/irda_robi/atmega16_dump/`). C'est le décodeur IR (protocole Robitronic)
qui envoie les passages à l'ATmega328PB sur son UART (38400 bauds).

| Fichier | Contenu |
|---|---|
| `flash.hex` | firmware (dont une partie dans la zone bootloader, 0x3F00) |
| `eeprom.hex` | configuration (quelques octets) |
| `avrdude-info` | relevé d'origine, avec les fusibles |

Fusibles d'origine : **lfuse 0x1F** (résonateur externe 16 MHz, BOD 4 V), **hfuse 0xC8**
(démarrage sur la zone bootloader : **indispensable**, le code y est). L'ATmega16 n'a pas d'efuse.

Programmateur sur **P2** (ISP de l'ATmega16), pas P1. Signature attendue : `1E 94 03`.

```
AD=$(ls -d ~/.platformio/packages/tool-avrdude* | head -1)
$AD/avrdude -C $AD/avrdude.conf -c usbtiny -p m16 \
  -U flash:w:flash.hex:i -U eeprom:w:eeprom.hex:i -U lfuse:w:0x1F:m -U hfuse:w:0xC8:m
```
