# ***Info***

- working at 100%
- pseudo led heartbeat.
- auto randomization or manual ID wrote in EEPROM at first run with correct CRC calculated.
- force reset ID pin at power On
- Attiny_X_4 / Attiny_X_5 compatible  with X = 2 | 4 | 8


# ***Hardware info:***

### BOM

- pcb -> view gerber
- attinyX4 (yes even 24 should be enought)
        ATTINY24-20SU around 1euro/1
- attinyX5 should work (haven't tested yet)
- led VISHAY or OSRAM or XXX, should be changed with less expensive? less powerfull?
        LED infrarouge 2835 W 0.2 SMD 940nm 3euros/5pieces
- ~~external quartz is gold-plating for this application. could be removed if cost is mandatory ?!?!~~
        8mhz smd 1,3euros/10
        ie: new version don't use anymore

### Emitter pinout
(check which end starts with -+ on the board :))

* 1 GND
* 2 VCC
* 3 MOSI
* 4 MISO
* 5 SCK
* 6 RST




# ***Software info:***

### Compiler Options:

- ATTinyCore no bootloader
- first designed for attiny85, should work even on attiny25 (1394bytes of flash and 17bytes compiled) (around 1700B as 11/feb/22)
~~- disable millis/micros() ~~
- need test with LTO/B.O.D/timer freq... default
- fully tested with attiny24
- Set 8mhz internal 


### Arduino ATtiny compiling

By default Arduino IDE doesn't support ATtiny boards, but you can add it manually:

File -> Settings
Additional Boards Manager URL:
add:
https://raw.githubusercontent.com/damellis/attiny/ide-1.6.x-boards-manager/package_damellis_attiny_index.json
(ATtinycore, not used currently: ~~http://drazzy.com/package_drazzy.com_index.json~~)

Now you can install the ATtiny boards from Board Manager.

### USBasp programming
probably you'll have to fix your driver:

driver: https://zadig.akeo.ie/

### USBtinyISP programming
Only tested on Debian without issues 

