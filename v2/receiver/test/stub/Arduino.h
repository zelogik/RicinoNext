// Minimal Arduino API for running the receiver firmware on a PC (tests only).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <vector>
#include <string>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

extern uint32_t fake_ms, fake_us;
extern int pin_in[32], pin_out[32];
extern uint8_t SREG;

inline uint32_t millis() { return fake_ms; }
inline uint32_t micros() { return fake_us; }
inline void delay(uint32_t ms) { fake_ms += ms; fake_us += ms * 1000; }
inline void delayMicroseconds(uint32_t us) { fake_us += us; }
inline void pinMode(int, int) {}
inline void digitalWrite(int p, int v) { pin_out[p] = v; }
inline int digitalRead(int p) { return pin_in[p]; }
inline void noInterrupts() {}
inline void interrupts() {}

class FakeStream {
public:
    std::deque<uint8_t> rx;
    std::vector<uint8_t> tx;
    void begin(long) {}
    int available() { return (int)rx.size(); }
    int read() { if (rx.empty()) return -1; uint8_t c = rx.front(); rx.pop_front(); return c; }
    size_t write(const uint8_t *d, size_t n) { tx.insert(tx.end(), d, d + n); return n; }
    size_t write(uint8_t c) { tx.push_back(c); return 1; }
    size_t print(const char *s) { size_t n = strlen(s); write((const uint8_t *)s, n); return n; }
};
extern FakeStream Serial;
