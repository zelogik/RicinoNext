/* PC test of src/protocol.h: same frame and EEPROM image as the first firmware (Software/Emitter/irda_robi),
 * CRC checked against frames captured from real Robitronic transponders. Built and run by test_emitter.py. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/protocol.h"

/* ---- first firmware, copied as is (Emitter.ino: getCRC8, getParity, setUniqueID, codeLoop) ---- */
static uint8_t getCRC8(const uint8_t *data, size_t len)
{
    uint8_t remainder = 0;
    for (size_t i = 0; i < len; i++) {
        remainder ^= *data++;
        for (uint8_t j = 8; j > 0; j--)
            remainder = (remainder & (1 << 7)) ? (uint8_t)((remainder << 1) ^ 0x07) : (uint8_t)(remainder << 1);
    }
    return remainder;
}

static uint8_t getParity(const uint8_t *data, uint8_t len)
{
    uint8_t parity = 0;
    for (uint8_t i = len; i > 0; i--) {
        uint8_t extract = *data, parityBit = 1;
        while (extract) {
            parityBit ^= extract & 1;
            extract >>= 1;
        }
        parity = (uint8_t)((parity << 1) | parityBit);
        data++;
    }
    return parity;
}

static void old_image(uint32_t txID, uint8_t a[6])
{
    memset(a, 0xFF, 6);                          /* erased EEPROM */
    a[3] = 0;
    a[2] = (uint8_t)(txID >> 16);
    a[1] = (uint8_t)(txID >> 8);
    a[0] = (uint8_t)txID;
    a[3] = getCRC8(a, 3);
    a[5] = getParity(a, 4);
}

static void old_frame(const uint8_t a[6], uint8_t out[40])   /* codeLoop(), pulse() recorded */
{
    uint8_t parityMask = 0x08, k = 0;
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t idMask = 0x80;
        out[k++] = 1;
        for (int j = 0; j < 8; j++) {
            out[k++] = (a[i] & idMask) ? 0 : 1;
            idMask >>= 1;
        }
        out[k++] = (a[5] & parityMask) ? 1 : 0;
        parityMask >>= 1;
    }
}

int main(void)
{
    /* real transponders (robi_Reverse EngineeringProtocol.svg): ID -> CRC */
    static const struct { uint32_t id; uint8_t crc; } cap[] = {
        {0xFFAA00, 0x69}, {0x012407, 0xEB}, {0x0F1206, 0x2D}, {0x436587, 0x67}, {0xFFFFFF, 0x0F}};
    for (unsigned i = 0; i < sizeof cap / sizeof cap[0]; i++) {
        uint8_t b[4];
        frame_bytes(cap[i].id, b);
        if (b[3] != cap[i].crc) {
            printf("FAIL crc of 0x%06lx: 0x%02x, captured 0x%02x\n", (unsigned long)cap[i].id, b[3], cap[i].crc);
            return 1;
        }
    }
    /* every ID of a 24-bit sweep (step 7) + a pseudo-random set: same slots, same EEPROM bytes */
    uint32_t n = 0, x = 12345;
    for (uint32_t k = 0; k < 2600000; k++) {
        uint32_t id = k < 2400000 ? k * 7 : ((x = x * 1103515245u + 12345u) >> 8) & 0xFFFFFF;
        uint8_t a[6], e[6], s_old[40], s_new[40];
        old_image(id, a);
        old_frame(a, s_old);
        memset(e, 0xFF, 6);
        eeprom_image(id, e);
        frame_slots(id, s_new, 1);
        if (memcmp(a, e, 6) || memcmp(s_old, s_new, 40)) {
            printf("FAIL id 0x%06lx differs from the first firmware\n", (unsigned long)id);
            return 1;
        }
        n++;
    }
    printf("protocol   : OK (CRC = %u Robitronic captures, %lu IDs identical to the first firmware)\n",
           (unsigned)(sizeof cap / sizeof cap[0]), (unsigned long)n);
    return 0;
}
