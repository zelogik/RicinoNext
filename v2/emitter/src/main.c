/* RicinoNext emitter: Robitronic-compatible IR transponder, ATtiny24/44/84 (emitter PCB) or ATtiny25/45/85.
 *
 * Successor of Software/Emitter/irda_robi (same frame, same EEPROM layout: a reflashed car keeps its ID).
 *  - frame timing exact to the CPU cycle (inline assembly, interrupts off): 10 us slots, 2 us pulses,
 *    103.5 us from byte to byte, like the original transponder; it no longer depends on the compiler;
 *  - a frame every 0.9 to 4.35 ms (random, like the original), about 3 times more than the first firmware;
 *  - new ID: 24 random bits from the jitter between the watchdog and CPU oscillators;
 *  - the stored ID is checked at every start (CRC / parity rewritten if damaged);
 *  - "new ID" key: tie MOSI (ISP header pin 3) to GND (pin 1) while powering up.
 * No Arduino core: no millis interrupt, ~1 KB of flash.
 */
#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <util/delay_basic.h>
#include "protocol.h"

#if F_CPU != 8000000UL
#error "timing written for 8 MHz: internal oscillator, CKDIV8 unprogrammed (lfuse 0xE2)"
#endif

#ifndef TX_ID
#define TX_ID 0              /* 0: random ID created at the first start; or a fixed ID, 1 .. 0xFFFFFE */
#endif

#if defined(__AVR_ATtiny24__) || defined(__AVR_ATtiny24A__) || defined(__AVR_ATtiny44__) || \
    defined(__AVR_ATtiny44A__) || defined(__AVR_ATtiny84__) || defined(__AVR_ATtiny84A__)
#define IR_BIT    _BV(PB2)   /* IR LED through the transistor (pin 14) */
#define LED_PORT  PORTA      /* heartbeat LED, PA7 (pin 15) */
#define LED_DDR   DDRA
#define LED_BIT   _BV(PA7)
#define KEY_PORT  PORTA      /* "new ID" key: MOSI = PA6 (pin 16) */
#define KEY_PIN   PINA
#define KEY_BIT   _BV(PA6)
#elif defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
#define IR_BIT    _BV(PB1)
#define LED_PORT  PORTB
#define LED_DDR   DDRB
#define LED_BIT   _BV(PB0)
#define KEY_PORT  PORTB
#define KEY_PIN   PINB
#define KEY_BIT   _BV(PB2)
#define WDTCSR    WDTCR
#else
#error "ATtiny24/44/84 or ATtiny25/45/85 only"
#endif

static uint8_t slots[FRAME_SLOTS];      /* IR_BIT (pulse) or 0, built once at start */

/* One frame, 414 us, cycle exact at 8 MHz (cycle counts in the comments). Interrupts are never
 * enabled in this firmware; they are disabled here anyway so the timing can not be stretched. */
static void send_frame(void)
{
    uint8_t sreg = SREG, base = PORTB & (uint8_t)~IR_BIT, bytes = 4, n, d, t;
    const uint8_t *p = slots;
    __asm__ volatile("cli");
    __asm__ volatile(
        "byte%=:               \n\t"
        "ldi  %[n], 10         \n\t"   /* 1                                                    */
        "slot%=:               \n\t"
        "ld   %[t], %a[p]+     \n\t"   /* 2                                                    */
        "or   %[t], %[b]       \n\t"   /* 1                                                    */
        "out  %[port], %[t]    \n\t"   /* 1   pulse starts (or nothing)                        */
        "ldi  %[d], 5          \n\t"   /* 15  = 1 + 5 * 3 - 1                                  */
        "1: dec %[d]           \n\t"
        "brne 1b               \n\t"
        "out  %[port], %[b]    \n\t"   /* 1   pulse ends: 16 cycles = 2 us                     */
        "ldi  %[d], 19         \n\t"   /* 57  = 1 + 19 * 3 - 1                                 */
        "2: dec %[d]           \n\t"
        "brne 2b               \n\t"
        "dec  %[n]             \n\t"   /* 1                                                    */
        "brne slot%=           \n\t"   /* 2   -> 80 cycles = 10 us per slot (79 for the last)  */
        "ldi  %[d], 8          \n\t"   /* 24                                                   */
        "3: dec %[d]           \n\t"
        "brne 3b               \n\t"
        "nop                   \n\t"   /* 1                                                    */
        "dec  %[c]             \n\t"   /* 1                                                    */
        "brne byte%=           \n\t"   /* 2   -> 828 cycles = 103.5 us from byte to byte       */
        : [p] "+e"(p), [c] "+r"(bytes), [n] "=&d"(n), [d] "=&d"(d), [t] "=&r"(t)
        : [b] "r"(base), [port] "I"(_SFR_IO_ADDR(PORTB))
        : "memory");
    SREG = sreg;
}

static uint32_t mix(uint32_t h, uint8_t b)      /* FNV-1a */
{
    return (h ^ b) * 16777619UL;
}

/* 24 random bits: the watchdog (128 kHz oscillator) and the CPU (8 MHz RC) drift independently, so the
 * CPU cycle count at each watchdog tick has random low bits. Plus the power-up content of the free RAM. */
static uint32_t random_id(void)
{
    extern uint8_t __heap_start;
    uint8_t top;
    uint32_t h = 2166136261UL;
    for (uint8_t *p = &__heap_start; p < &top - 8; p++)
        h = mix(h, *p);
    TCCR0B = _BV(CS00);                         /* Timer0 counts CPU cycles */
    for (;;) {
        for (uint8_t i = 0; i < 64; i++) {      /* ~1 s, only when a new ID is made */
            WDTCSR = _BV(WDIF) | _BV(WDIE);     /* 16 ms, interrupt mode (polled: interrupts are off) */
            while (!(WDTCSR & _BV(WDIF)))
                ;
            h = mix(h, TCNT0);
        }
        uint32_t id = h & 0xFFFFFFUL;
        if (id && id <= ID_MAX) {
            WDTCSR = _BV(WDIF);                 /* watchdog off again */
            TCCR0B = 0;
            return id;
        }
    }
}

static uint16_t rng;
static uint16_t rand16(void)                    /* xorshift16 */
{
    rng ^= rng << 7;
    rng ^= rng >> 9;
    rng ^= rng << 8;
    return rng;
}

/* heartbeat: on 40 ms, off 120, on 50, off 700. After a new ID: 12 quick blinks first. */
static const uint16_t BEAT[4] = {40, 120, 50, 700};

int main(void)
{
    MCUSR = 0;
    wdt_disable();
    DDRB |= IR_BIT;
    LED_DDR |= LED_BIT;
    LED_PORT |= LED_BIT;                        /* first heartbeat step: on */

    KEY_PORT |= KEY_BIT;                        /* internal pull-up: the pin floats otherwise */
    _delay_us(50);
    uint8_t reset = !(KEY_PIN & KEY_BIT);       /* MOSI tied to GND at power-up */
    KEY_PORT &= (uint8_t)~KEY_BIT;

    uint8_t e[6], good[6];
    eeprom_read_block(e, (const void *)0, sizeof e);
    uint32_t id = e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16);
    uint8_t fresh = 0;
    if (TX_ID)
        id = TX_ID;
    else if (reset || e[5] == 0xFF || !id || id > ID_MAX) {
        id = random_id();
        fresh = 1;
    }
    eeprom_image(id, good);
    for (uint8_t i = 0; i < sizeof good; i++) {
        if (i == 4)
            continue;                           /* unused byte, left as it is */
        if (e[i] != good[i]) {                  /* new ID, or CRC / parity damaged: rewrite */
            eeprom_update_byte((uint8_t *)(uint16_t)i, good[i]);
            fresh = 1;
        }
    }
    frame_slots(id, slots, IR_BIT);
    rng = (uint16_t)(id ^ (id >> 8)) | 1;       /* each car its own sequence of delays */

    uint8_t step = 0, quick = fresh ? 24 : 0;
    uint16_t us = 0, ms = 0;
    for (;;) {
        send_frame();
        uint16_t gap = 900 + rand16() % 3451;   /* 0.9 to 4.35 ms, like the original */
        _delay_loop_2(gap * 2);                 /* 4 cycles = 0.5 us per count */

        us += gap + 450;                        /* + frame (414 us) and this loop */
        while (us >= 1000) {
            us -= 1000;
            ms++;
        }
        if (ms >= (quick ? 60 : BEAT[step])) {
            ms = 0;
            if (quick)
                quick--;
            else
                step = (step + 1) & 3;
            if ((quick ? quick : step) & 1)
                LED_PORT &= (uint8_t)~LED_BIT;
            else
                LED_PORT |= LED_BIT;
        }
    }
}
