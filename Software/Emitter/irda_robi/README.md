Hardware info:
- pcb -> view gerber
- attinyX4 (yes even 24 should be enought)
        ATTINY24-20SU around 1euro/1
- attinyX5 should work (haven't tested yet)
- led VISHAY or OSRAM or XXX, should be changed with less expensive? less powerfull?
        LED infrarouge 2835 W 0.2 SMD 940nm 3euros/5pieces
- external quartz is gold-plating for this application. could be removed if cost is mandatory ?!?!
        8mhz smd 1,3euros/10
        ie: new version don't use anymore


Software info:
- ATTinyCore no bootloader
- first designed for attiny85, should work even on attiny25 (1394bytes of flash and 17bytes compiled)
- disable millis/micros()
- need test with LTO/B.O.D/timer freq... default
- fully tested with attiny24


done:
- working at 100%
- pseudo led heartbeat.
- auto randomization or manual IR code wrote in EEPROM at first run with correct CRC calculated.

todo:
- nothing?