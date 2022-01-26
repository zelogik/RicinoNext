Current developement:

    - Emitter:
        IRDA transmitter (robitronic clone)
          attiny24

    - Receiver:
        - IRDA receiver
          atmega16/16a -> receive and decode IRDA signal, send uart command.
          atmega328/328p/.. -> receive uart from atmega16, convert to an I2C slave.
        
        - IRDA reciver extender
          allow wider track and so better detection.
    
    - Controller:
        - ESP32/ESP8266:
          controller is a I2C master + HTTP/websocket server

        - SAMD21 + ESP01:
          SAMD21 is a I2C master
          ESP01 -> receive UART signal from SAMD21 and become a HTTP/websocket server.

