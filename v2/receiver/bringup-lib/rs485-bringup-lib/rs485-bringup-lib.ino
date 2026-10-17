/*
 * RicinoNext RS485 bring-up test, receiver side, written with the Arduino libraries
 * (same behaviour as ../bringup, which uses registers only: two independent implementations).
 *
 *   RX = PB0 (D8), TX = PB1 (D9), 9600 8N1, with SoftwareSerial (env "softserial")
 *   or AltSoftSerial (env "altsoft", the library of the real firmware). PD0 / PD1 are not touched.
 *   LED = PD2 : digitalWrite(2), active low
 *   address : jumpers SJ3/SJ4/SJ5 = D5/D6/D7 to GND -> 21 + (0..7)
 *
 *   power-up : LED blinks the gate number, then "H21\n"
 *   "T21\n"  : LED toggles, answers "G21\n"     "T00\n": LED toggles, no answer
 */
#include <Arduino.h>

#define BAUD 9600
#define LED 2

#if USE_ALTSOFT
#include <AltSoftSerial.h>
AltSoftSerial bus;                       // RX = D8 = PB0, TX = D9 = PB1 (fixed by Timer1)
#else
#include <SoftwareSerial.h>
SoftwareSerial bus(8, 9);                // RX = D8 = PB0, TX = D9 = PB1
#endif

uint8_t address = 21;
bool ledOn = false;
char line[8];
uint8_t len = 0;

void led(bool on) { ledOn = on; digitalWrite(LED, on ? LOW : HIGH); }

void say(char kind) {
    delay(2);
    bus.write(kind);
    bus.write('0' + address / 10);
    bus.write('0' + address % 10);
    bus.write('\n');
}

void setup() {
    pinMode(LED, OUTPUT);
    led(false);
    pinMode(5, INPUT_PULLUP);
    pinMode(6, INPUT_PULLUP);
    pinMode(7, INPUT_PULLUP);
    delay(2);
    address = 21 + (digitalRead(5) == LOW ? 1 : 0) + (digitalRead(6) == LOW ? 2 : 0) + (digitalRead(7) == LOW ? 4 : 0);
    for (uint8_t i = 0; i <= address - 21; i++) { led(true); delay(250); led(false); delay(250); }
    bus.begin(BAUD);
    delay(100 + (address - 21) * 50);
    say('H');
}

void loop() {
    while (bus.available()) {
        char c = bus.read();
        if (c == '\n' || c == '\r') {
            if (len == 3 && line[0] == 'T') {
                uint8_t a = (line[1] - '0') * 10 + (line[2] - '0');
                if (a == address || a == 0) led(!ledOn);
                if (a == address) say('G');
            }
            len = 0;
        } else if (len < sizeof(line)) {
            line[len++] = c;
        } else {
            len = 0;
        }
    }
}
