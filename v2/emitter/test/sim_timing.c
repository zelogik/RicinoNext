/* Runs the real firmware (.elf) in the simavr AVR simulator and measures the IR LED pin to the CPU cycle:
 * pulse widths, slot and byte spacing, frame length, delay between frames; decodes every frame back to an
 * ID and checks start / parity / CRC. Built and run by test_emitter.py (needs PlatformIO's tool-simavr).
 *
 *   sim_timing <firmware.elf> <mcu> <seconds> <ee: 12 hex digits or "-"> <key: 0|1>
 *   key 1 = "new ID" key held (MOSI to GND) at power-up
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim_avr.h"
#include "sim_elf.h"
#include "avr_ioport.h"
#include "avr_eeprom.h"
#include "../src/protocol.h"

#define MAXE 400000
static avr_t *avr;
static uint64_t rise[MAXE], fall[MAXE];
static unsigned nr, nf, led_changes;

static void ir_change(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    if (value && nr < MAXE) rise[nr++] = avr->cycle;
    if (!value && nf < MAXE && nf < nr) fall[nf++] = avr->cycle;
}

static void led_change(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)value; (void)param;
    led_changes++;
}

int main(int argc, char **argv)
{
    if (argc < 6) return 2;
    const char *mcu = argv[2];
    int x5 = strstr(mcu, "5") != NULL;          /* attiny25/45/85 */
    elf_firmware_t fw;
    memset(&fw, 0, sizeof fw);
    if (elf_read_firmware(argv[1], &fw)) return 2;
    fw.frequency = 8000000;
    strcpy(fw.mmcu, mcu);
    avr = avr_make_mcu_by_name(mcu);
    if (!avr) return 2;
    avr_init(avr);
    avr_load_firmware(avr, &fw);

    if (strcmp(argv[4], "-")) {                 /* preload the EEPROM (an emitter that already has an ID) */
        uint8_t ee[6];
        for (int i = 0; i < 6; i++) sscanf(argv[4] + 2 * i, "%2hhx", &ee[i]);
        avr_eeprom_desc_t d = {.ee = ee, .offset = 0, .size = 6};
        avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &d);
    }
    char ir_port = 'B', led_port = x5 ? 'B' : 'A', key_port = x5 ? 'B' : 'A';
    int ir_pin = x5 ? 1 : 2, led_pin = x5 ? 0 : 7, key_pin = x5 ? 2 : 6;
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(ir_port), ir_pin), ir_change, NULL);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(led_port), led_pin), led_change, NULL);
    if (atoi(argv[5])) {                        /* key: pin wired to GND (wins over the internal pull-up) */
        avr_ioport_external_t key = {.name = key_port, .mask = 1 << key_pin, .value = 0};
        avr_ioctl(avr, AVR_IOCTL_IOPORT_SET_EXTERNAL(key_port), &key);
    }

    uint64_t end = (uint64_t)(atof(argv[3]) * 8000000);
    while (avr->cycle < end) {
        int st = avr_run(avr);
        if (st == cpu_Done || st == cpu_Crashed) { printf("crashed\n"); return 1; }
    }

    /* pulses */
    unsigned wmin = 1000, wmax = 0;
    for (unsigned i = 0; i < nf; i++) {
        unsigned w = (unsigned)(fall[i] - rise[i]);
        if (w < wmin) wmin = w;
        if (w > wmax) wmax = w;
    }
    /* frames: pulses closer than 250 us belong to the same frame */
    unsigned frames = 0, bad = 0, ids = 0;
    uint32_t id = 0, last_id = 0;
    double gmin = 1e9, gmax = 0, gsum = 0, lmin = 1e9, lmax = 0;
    unsigned ng = 0;
    for (unsigned i = 0; i < nf;) {
        unsigned j = i + 1;
        while (j < nf && rise[j] - rise[j - 1] < 2000) j++;
        if (j == nf && avr->cycle - rise[j - 1] < 4000) break;      /* last frame may be cut */
        uint8_t s[FRAME_SLOTS] = {0};
        int ok = 1;
        for (unsigned k = i; k < j; k++) {                           /* every pulse exactly on a slot */
            uint64_t o = rise[k] - rise[i];
            unsigned byte = (unsigned)(o / 828), slot = (unsigned)((o % 828) / 80);
            if (byte > 3 || (o % 828) % 80 || slot > 9) { ok = 0; break; }
            s[byte * 10 + slot] = 1;
        }
        uint8_t b[4] = {0};
        for (int k = 0; ok && k < 4; k++) {
            if (!s[k * 10]) ok = 0;                                  /* start pulse */
            for (int m = 0; m < 8; m++) b[k] = (uint8_t)((b[k] << 1) | !s[k * 10 + 1 + m]);
            if (s[k * 10 + 9] != parity_pulse(b[k])) ok = 0;
        }
        if (ok && crc8(b, 3) != b[3]) ok = 0;
        if (ok) {
            id = b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
            if (!ids || id != last_id) ids++;
            last_id = id;
        } else bad++;
        double len = (fall[j - 1] - rise[i]) / 8.0;
        if (len < lmin) lmin = len;
        if (len > lmax) lmax = len;
        if (frames) {
            double g = (rise[i] - fall[i - 1]) / 8.0;               /* from the end of the previous frame */
            if (g < gmin) gmin = g;
            if (g > gmax) gmax = g;
            gsum += g;
            ng++;
        }
        frames++;
        i = j;
    }
    uint8_t ee[6];
    avr_eeprom_desc_t d = {.ee = ee, .offset = 0, .size = 6};
    avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
    printf("pulse_cycles %u %u\n", wmin, wmax);
    printf("frames %u bad %u ids %u id %06lx\n", frames, bad, ids, (unsigned long)last_id);
    printf("frame_us %.1f %.1f\n", lmin, lmax);
    printf("gap_us %.1f %.1f %.1f\n", gmin, gmax, ng ? gsum / ng : 0);
    printf("first_frame_ms %.1f\n", nr ? rise[0] / 8000.0 : -1);
    printf("led_changes %u\n", led_changes);
    printf("eeprom %02x%02x%02x%02x%02x%02x\n", d.ee[0], d.ee[1], d.ee[2], d.ee[3], d.ee[4], d.ee[5]);
    return 0;
}
