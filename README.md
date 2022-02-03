# RicinoNext
Dream to be yet another self container DIY hardware solution for a complete Lap Counter with a bunch of micro-controller (attiny,atmega,samd21,esp, and more ...) ?! focused on RC car/drone ?!

Why so much code and headache? Because I haven't found open-source lap counter with open-source hardware working on open-source OS.. in 2019... neither in 2022. Seriouly, I have more than 20 rc cars, so put a commercial product (ir or rf transmitter) on each will cost me to much money, and the software will work only on windows/android product. So first, I decided to try and fork ricino, but code was (sorry...) (hum...) like trash... but almost working and a very good base for me :-). Zround was the only option with ricino, but I haven't any Windows at home, and Qemu/vmware each time was no more cool in 2019. Only plenty of BSD/Linux servers, so my first technical review was running a web server on docker with REMI (python web framework). But as I have plenty of ESP8266 and ESP01 keeping all around, start ESP WEBUI. Maybe change to ESP32 or samd21+ESP01 later...


Hardware Setup (and comparaison):

    - Transmitter option: (cheap to expensive)
        - Nothing -> led barrier, Only one car on the track. (cheapest...) (POSSIBLE TO ADAPT)
        - IR -> RC5/Ricino/easytimer, 38khz, 110ms between each data pulse (POSSIBLE TO ADAPT)
        - IR -> CoreIR/I-Lap, 370khz , 20 or 30ms between each data pulse ?!?
        - IRDA -> Robitronic/Ezlap/etc.., 500us data pulse + 1-5ms waiting (DEFAULT VERSION)
        - RFID -> Kyosho, passive RFID, 5-10mm max gap between loop and transmitter
        - UHF -> AMBrc, active "RFID", look at github clone...


    - Receiver:
        - Led Barrier, only for solo mode
        - Ricino"Clone" working but missing many lap when fast, don't use if possible(same pcb than IRDA one)
        - IRDA, DIY PCB available, Receiver + 3x extenders

    - Display:
        - ESP32 + OLED display
        - Smartphone/Tablet/PC, with javascript compatible browser connected on the ESP32
        - future: app compatible with JSON connection
        - future: add Zround protocol


Software:

    - Need Edit/update on what done and what remaining!


Hardware Addons, plugins. Everything connected on the I2C bus (by implementation order priority):

    - Receiver (of course) -> Send trig time
    - Voice Speaker -> just send time ? (add cheap MP3 arduino reader?)
    - Simple Buzzer -> Start Buz, Last Buz, Best Buz, Final Buz, (or send directly frequency on the i2C bus?)
    - Light/Relay -> Port + light intensity ( + delay off / warm up phase/ etc..)
    - RGB Light -> Port + color

    - !?NextStep? RFID tag -> why not ... really cheap, and add pit stop possibility
    - !?NextStep? Display ->  a big dot matrix screen could be very nice on I2C... ?
    - !?NextStep? SD Card -> Save History, statistics for backup etc... (add complexity on the UI!)
    - !?NextStep? ESP8266/ESP32 -> direct connection to database mariadb/postgresql/... ?
    - ?!NextStep? Temperature/pressure -> get info on track "climat/weather"
    - ?!NextStep? Endless possibility -> function MUST/NEED to be "easily" added to the Controller software.


Quick architecture diagram:
view: Architecture Diagram.svg


Hardware/Software priority and current dev now:

    Tranmitter: attinyX5 -> attiny24 (less expensive and available)

    Receiver: ATmega16/IRDA->UART-38400 + ATmega328p/UART->I2C (and maybe CAN in future for longer distances...)

    Display: Read I2C bus + OLED + HTTP/websocket
 

 Todo:
    - Finalize a clean JSON api
    - Draft a working HTTP/CSS/Javascript with only small memory available.
    - Join ALL the code i have wrote since months/years to debug each part to get an "efficient" code delay()-free and fast as possible :-D
    - Join old code with new code, time to get a debug version to work on, even without ALL hardware available!
    - write a Wiki/howto
    - play, race!! but essentially to find bug of course :-)