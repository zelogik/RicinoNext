/*
 * RicinoNext RS485 bring-up test, receiver side (ATmega328PB @ 16 MHz). Registers only: no library,
 * no Arduino pin numbers, nothing else running.
 *
 *   RX  = PB0 (pin 12)        bit-banged 9600 8N1 (pin read in the middle of each bit)
 *   TX  = PB1 (pin 13)        bit-banged 9600 8N1, idle high
 *   PD0 / PD1 (decoder UART) are not touched.
 *   LED = PD2 (pin 32)        active low (LED3 goes from VCC to the pin)
 *   address: solder jumpers SJ3/SJ4/SJ5 = PD5/PD6/PD7, closed = GND -> 21 + (0..7)
 *
 *   power-up : LED blinks the gate number (1 = 21 ... 8 = 28), then sends "H21\n" (hello)
 *   "T21\n"  : LED toggles, answers "G21\n"        (21 = this gate's address)
 *   "T00\n"  : LED toggles, no answer               (every gate)
 *
 * Controller side: receiver/bringup/rs485_bringup_controller.py
 * Env "pintest" (PIN_TEST=1): PB1 low 1 s / high 1 s, LED follows (multimeter check of the TX path).
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define BAUD 9600
#define BIT_US 104                       // 1 / 9600 s = 104.2 us

static void led(bool on) {               // active low
    if (on) PORTD &= ~_BV(PD2); else PORTD |= _BV(PD2);
}

#if PIN_TEST
// ---------------------------------------------------------------------------------------------
int main() {
    DDRB |= _BV(PB1);
    DDRD |= _BV(PD2);
    for (;;) {
        PORTB &= ~_BV(PB1); led(true);  _delay_ms(1000);      // PB1 low, LED on
        PORTB |= _BV(PB1);  led(false); _delay_ms(1000);      // PB1 high, LED off
    }
}
#else
// ---------------------------------------------------------------------------------------------
static bool ledOn = false;
static uint8_t address = 21;

static void ledToggle() { ledOn = !ledOn; led(ledOn); }

// TX on PB1, 8N1, LSB first. Interrupts off during a byte (none are used anyway).
static void txByte(uint8_t b) {
    uint8_t sreg = SREG;
    cli();
    PORTB &= ~_BV(PB1);                  // start bit
    _delay_us(BIT_US);
    for (uint8_t i = 0; i < 8; i++) {
        if (b & 1) PORTB |= _BV(PB1); else PORTB &= ~_BV(PB1);
        b >>= 1;
        _delay_us(BIT_US);
    }
    PORTB |= _BV(PB1);                   // stop bit, then idle high
    _delay_us(BIT_US);
    SREG = sreg;
}

static void say(char kind) {             // "H21\n" / "G21\n"
    _delay_ms(2);                        // let the controller's transceiver turn around
    txByte(kind);
    txByte('0' + address / 10);
    txByte('0' + address % 10);
    txByte('\n');
}

// RX on PB0, bit-banged: wait for the start bit, then read each bit in its middle
static void rxInit() {
    DDRB &= ~_BV(PB0);                   // input
    PORTB |= _BV(PB0);                   // pull-up: idle high if nothing drives it
}

static int rxByte() {                    // -1 when no start bit / glitch / bad stop bit
    if (PINB & _BV(PB0)) return -1;      // line idle (high): nothing
    _delay_us(BIT_US / 2);               // middle of the start bit
    if (PINB & _BV(PB0)) return -1;      // too short: glitch
    uint8_t b = 0;
    for (uint8_t i = 0; i < 8; i++) {    // LSB first
        _delay_us(BIT_US);
        b >>= 1;
        if (PINB & _BV(PB0)) b |= 0x80;
    }
    _delay_us(BIT_US);                   // middle of the stop bit
    return (PINB & _BV(PB0)) ? b : -1;   // stop bit must be high (else framing error)
}

static uint8_t readAddress() {
    DDRD &= ~(_BV(PD5) | _BV(PD6) | _BV(PD7));
    PORTD |= _BV(PD5) | _BV(PD6) | _BV(PD7);       // pull-ups
    _delay_ms(2);
    uint8_t id = (PIND & _BV(PD5) ? 0 : 1) | (PIND & _BV(PD6) ? 0 : 2) | (PIND & _BV(PD7) ? 0 : 4);
    return 21 + id;
}

int main() {
    DDRD |= _BV(PD2);                    // LED
    led(false);
    PORTB |= _BV(PB1);                   // TX idle high before becoming an output
    DDRB |= _BV(PB1);
    address = readAddress();
    for (uint8_t i = 0; i <= address - 21; i++) {  // blink the gate number
        led(true); _delay_ms(250);
        led(false); _delay_ms(250);
    }
    rxInit();
    for (uint8_t i = 0; i < address - 21; i++) _delay_ms(50);   // gates powered together: one at a time
    _delay_ms(100);
    say('H');

    char line[8];
    uint8_t len = 0;
    for (;;) {
        int c = rxByte();
        if (c < 0) continue;
        if (c == '\n' || c == '\r') {
            if (len == 3 && line[0] == 'T' && line[1] >= '0' && line[1] <= '9' && line[2] >= '0' && line[2] <= '9') {
                uint8_t a = (line[1] - '0') * 10 + (line[2] - '0');
                if (a == address || a == 0) ledToggle();
                if (a == address) say('G');
            }
            len = 0;
        } else if (len < sizeof(line)) {
            line[len++] = c;
        } else {
            len = 0;                     // garbage: start over
        }
    }
}
#endif
