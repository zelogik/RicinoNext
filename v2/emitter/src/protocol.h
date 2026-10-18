/* Robitronic transponder frame, as sent by the first RicinoNext emitter (Software/Emitter/irda_robi)
 * and decoded by the gates. Plain C, no AVR specifics: test/test_emitter.py compiles it on a PC.
 *
 * On air: 4 bytes = the 24-bit ID, least significant byte first, then CRC-8 (polynomial 0x07, init 0)
 * of these 3 bytes. Each byte takes 10 slots of 10 us:
 *   slot 0      start: always a pulse
 *   slots 1..8  data bits, most significant first, inverted (pulse = 0)
 *   slot 9      parity: a pulse when the byte has an even number of 1 bits
 * A pulse is a 2 us flash of the IR LED at the start of its slot.
 */
#ifndef RICINO_PROTOCOL_H
#define RICINO_PROTOCOL_H
#include <stdint.h>

#define ID_MAX  0xFFFFFEUL               /* 0 and 0xFFFFFF are refused (erased / unset) */
#define FRAME_SLOTS 40

static inline uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static inline uint8_t parity_pulse(uint8_t b)          /* 1 = even number of 1 bits */
{
    uint8_t p = 1;
    while (b) {
        p ^= b & 1;
        b >>= 1;
    }
    return p;
}

static inline void frame_bytes(uint32_t id, uint8_t out[4])
{
    out[0] = (uint8_t)id;
    out[1] = (uint8_t)(id >> 8);
    out[2] = (uint8_t)(id >> 16);
    out[3] = crc8(out, 3);
}

/* slots[i] = pulse (the value given) or 0, for the 40 slots of the frame */
static inline void frame_slots(uint32_t id, uint8_t slots[FRAME_SLOTS], uint8_t pulse)
{
    uint8_t b[4];
    frame_bytes(id, b);
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t *s = slots + 10 * i;
        s[0] = pulse;
        for (uint8_t j = 0; j < 8; j++)
            s[1 + j] = (b[i] & (0x80 >> j)) ? 0 : pulse;
        s[9] = parity_pulse(b[i]) ? pulse : 0;
    }
}

/* EEPROM image, same layout as the first firmware so a car keeps its ID:
 * 0..2 ID (LSB first), 3 CRC-8, 4 unused, 5 parity bits (bit 3 = byte 0 ... bit 0 = CRC), 0xFF = no ID yet */
static inline void eeprom_image(uint32_t id, uint8_t e[6])
{
    uint8_t p = 0;
    frame_bytes(id, e);
    for (uint8_t i = 0; i < 4; i++)
        p = (uint8_t)((p << 1) | parity_pulse(e[i]));
    e[5] = p;
}

#endif
