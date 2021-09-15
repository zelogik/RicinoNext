# RicinoNext
A Robitronic compatible Lap Counter.


PC free Lap counter, just a mix of lot of microcontroller, attinyX5 x_number-of-cars + Atmega16 x3 + atmega328 x3 + ESP32 x1 

PC/Tablet/Smartphone is only needed if you want bigger screen by HTML/JAVASCRIPT/CSS/WEBSOCKET  :-D


1. Emitter Side:
  * (useless as irda protocol works?) CoreIR : almost done (need test, and upload), like Ilap ?
  * (useless as irda protocol works?) NEC5-"fast" : almost done (need test, and upload)
  * IRDA: Robitronic/easylap, Hardware + software -> Done, cost is around 2-3 Euros, attiny_x5/x4 (fit inside attiny2x version, YES!)
  * RFID : Kyosho, no work
  * UFHD : AMB-rc, no work
  

2. Receiver Side:
  * 3 bridges for check point : 3x 1 master + 3 or more slave.. oups... "secondary" units, master is around 6Euros/unit and slave 2euros/unit
  * Hardware : Design PCB : done, need ordering, consist of 1 atmega16 for irda decoding/uart + 1 atmega328 for i2c/uart
  * Software : 95% done, miss some testing
  * The 3 main bridges are connected by i2c to the Display, main Control unit

3. Control Unit:
  * Hardware: ESP32 + display and http/websocket server

  * 60% done. Old concept worked, need to add i2c communication AND checkpoint algo



Update:
 
15/09/21: Only IRDA emitter code/wiring/pcb uploaded
