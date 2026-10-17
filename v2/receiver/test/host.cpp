// Runs the real receiver firmware on a PC, driven by text commands on stdin (see test_receiver.py):
//   t <ms> | dec <hex> | bus <hex> | loop <n> | out | dout | addr | led
#include "Arduino.h"
uint32_t fake_ms = 1000, fake_us = 1000000;
int pin_in[32], pin_out[32];
uint8_t SREG = 0;
FakeStream Serial;
#include "../RicinoNext-receiver/RicinoNext-receiver.ino"

static void push_hex(FakeStream &s, const char *h) {
    while (h[0] && h[1] && h[0] != '\n') {
        char b[3] = {h[0], h[1], 0};
        s.rx.push_back((uint8_t)strtol(b, NULL, 16));
        h += 2;
    }
}

static void print_hex(std::vector<uint8_t> &v) {
    for (uint8_t c : v) printf("%02x", c);
    printf("\n");
    fflush(stdout);
    v.clear();
}

int main(int argc, char **argv) {
    for (int i = 0; i < 32; i++) pin_in[i] = HIGH;         // pull-ups: jumpers open
    int jumpers = argc > 1 ? atoi(argv[1]) : 0;              // bit0 SJ3, bit1 SJ4, bit2 SJ5 closed
    if (jumpers & 1) pin_in[ID_PIN0] = LOW;
    if (jumpers & 2) pin_in[ID_PIN1] = LOW;
    if (jumpers & 4) pin_in[ID_PIN2] = LOW;
    setup();
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        if (!strncmp(line, "t ", 2)) { uint32_t n = atol(line + 2); fake_ms += n; fake_us += n * 1000; }
        else if (!strncmp(line, "dec ", 4)) push_hex(Serial, line + 4);
        else if (!strncmp(line, "bus ", 4)) push_hex(rs485, line + 4);
        else if (!strncmp(line, "loop ", 5)) { int n = atoi(line + 5); while (n--) { loop(); fake_us += 30; } }
        else if (!strncmp(line, "out", 3)) print_hex(rs485.tx);
        else if (!strncmp(line, "dout", 4)) print_hex(Serial.tx);
        else if (!strncmp(line, "addr", 4)) { printf("%d\n", address); fflush(stdout); }
        else if (!strncmp(line, "led", 3)) { printf("%d\n", pin_out[LED_PIN]); fflush(stdout); }
    }
    return 0;
}
