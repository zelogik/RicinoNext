/*
 * RicinoNext receiver (gate) firmware, version 2: one firmware for every gate.
 *
 * Board: receiver PCB "IRDA_I2C" (2021-09). J1: 1-2 power, 3 SCL (PC5), 4 SDA (PC4), 5 PB1, 6 PB0.
 *        (the schematic says ATmega328P, the boards are fitted with a 328PB: signature 1E 95 16)
 *        P1 = ISP of this ATmega328 (1 GND, 2 VCC, 3 MOSI, 4 MISO, 5 SCK, 6 RESET); P2 = ISP of the ATmega16.
 *        ATmega16 = Robitronic IR decoder; ATmega328PB @ 16 MHz = this firmware, between the decoder
 *        (hardware UART, 38400) and the controller.
 *
 * Link to the controller (compile option LINK_RS485):
 *   1 (default) RS485, AltSoftSerial on D8 (PB0, RX) / D9 (PB1, TX), main connector J1 pins 6 / 5, auto-direction
 *               transceiver (HW-519 or any MAX485 module with automatic flow control). 57600 8N1.
 *   0           I2C slave (legacy wiring, J1 SDA/SCL).
 *
 * Gate address: solder jumpers SJ3/SJ4/SJ5 (PD5/PD6/PD7, closed = to GND, read with pull-ups):
 *   none = 21 (finish line), SJ3 = 22, SJ4 = 23, SJ3+SJ4 = 24, ... up to 28.
 *
 * Every link carries the same 13-byte packets (see controller/ricino/core.py):
 *   0x84 pass   : 84 | addr | crc | ID[4] LE | decoder time ms[4] LE | hit | strength
 *   0x83 time   : 83 | addr | crc | decoder time ms[4] LE | 0...
 *   0x82 status : 82 | addr | crc | state | clock delta | uart buffer | loop time | err decoder | err queue |
 *                 err link | queued | firmware version | flags (bit0 decoder silent, bit1 link lost in race)
 *   crc = Dallas/Maxim CRC-8 of bytes 3..12 (3..6 for 0x83).
 * RS485 framing (controller/ricino/rs485.py):
 *   request  A5 | addr | cmd | crc8(addr, cmd)          cmd 01 poll, 02 repeat last reply, 8A start, 8F stop
 *   reply    5A | addr | 0D | packet[13] | crc8(addr, 0D, packet)          addr 0 = broadcast, no reply
 *
 * Changes vs version 1 (Software/Receiver/irda_robi/RicinoNext-receiver.ino):
 *   - RS485 link, address from the solder jumpers (was hard-coded 21);
 *   - nothing blocks the loop any more (v1 waited up to 5 ms per byte and read past a timeout);
 *   - decoder frames are length-checked (v1 could overflow its 13-byte buffer) and resynchronised;
 *   - an 8-packet queue: passes are no longer held back by time stamps or lost when several cars pass
 *     between two polls;
 *   - a lost poll during a race no longer resets the decoder clock (v1 went DISCONNECTED then
 *     restarted the decoder: every time after that was wrong);
 *   - error counters reported to the controller, decoder watchdog, LED flash on each pass;
 *   - I2C callbacks only copy data, the commands run in loop().
 */
#include <Arduino.h>

// ----------------------------------------------------------------------------
//  Configuration
// ----------------------------------------------------------------------------
#ifndef LINK_RS485
#define LINK_RS485 1            // 1: RS485 (AltSoftSerial D8/D9)   0: I2C slave (legacy)
#endif
#define FW_VERSION 3
#define RS485_BAUD 57600        // must match the controller (system page > Matériel)
#define DECODER_BAUD 38400      // ATmega16 Robitronic decoder
#define BASE_ADDRESS 21         // address with no jumper closed (21 = finish line)
#define LED_PIN 2               // PD2
#define LED_ACTIVE_LOW 1        // LED3 goes from VCC to the pin (through R5): lit when the pin is LOW
#define ID_PIN0 5               // PD5, SJ3
#define ID_PIN1 6               // PD6, SJ4
#define ID_PIN2 7               // PD7, SJ5
#define QUEUE_SIZE 8            // passes waiting for the controller
#define LINK_TIMEOUT_MS 2000    // no frame from the controller for 2 s: link lost
#define DECODER_TIMEOUT_MS 3000 // a running decoder sends a time stamp every second
#define KEEPALIVE_MS 5000       // idle: decoder reset sent every 5 s (keeps it quiet)
#define FRAME_GAP_MS 5          // max silence inside a frame before resynchronising
#define REPLY_DELAY_US 200      // RS485: let the controller's transceiver turn around

#if LINK_RS485
#include <AltSoftSerial.h>
AltSoftSerial rs485;            // RX = D8 (PB0/ICP1), TX = D9 (PB1/OC1A): fixed by Timer1
#else
#include <Wire.h>
#endif

#define PACKET_SIZE 13
#define INC(x) do { if ((x) < 255) (x)++; } while (0)

enum : uint8_t { PKT_STATUS = 0x82, PKT_TIME = 0x83, PKT_ID = 0x84 };
enum : uint8_t { CMD_POLL = 0x01, CMD_REPEAT = 0x02, CMD_START = 0x8A, CMD_STOP = 0x8F };
enum : uint8_t { REQ_SYNC = 0xA5, REP_SYNC = 0x5A };
enum State : uint8_t { DISCONNECTED = 0, CONNECTED = 1, RACE = 3 };   // values read by the controller
struct Packet { uint8_t b[PACKET_SIZE]; };   // before any function: the .ino prototypes use it

// ----------------------------------------------------------------------------
//  State
// ----------------------------------------------------------------------------
uint8_t address = BASE_ADDRESS;
volatile State state = DISCONNECTED;
volatile uint32_t lastLinkMs = 0;       // last valid request for this gate (ping/pong)
uint32_t lastBusByteMs = 0;             // last byte heard on the bus (any)
uint32_t lastBusFrameMs = 0;            // last valid request heard (any gate)
uint32_t lastDecoderMs = 0;             // last byte from the decoder
uint32_t lastKeepaliveMs = 0;
uint32_t raceStartMs = 0;
uint32_t decoderTime = 0;               // last decoder time stamp (ms since start)
int8_t clockDelta = 0;                  // local race time - decoder time, ms (drift / missed start)
uint32_t flashUntil = 0;

// error counters, saturating, reset at each start
volatile uint8_t errDecoder = 0;        // malformed / truncated decoder frames
volatile uint8_t errQueue = 0;          // passes dropped: queue full (controller not polling)
volatile uint8_t errLink = 0;           // bad request CRC, repeated replies
uint8_t worstLoop = 0;                  // worst loop time, 1/10 ms

bool decoderSilent() { return state == RACE && millis() - lastDecoderMs > DECODER_TIMEOUT_MS; }
bool linkLost() { return millis() - lastLinkMs > LINK_TIMEOUT_MS; }

// ----------------------------------------------------------------------------
//  CRC-8 Dallas/Maxim (same as the controller and the emitters)
// ----------------------------------------------------------------------------
uint8_t crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        uint8_t extract = *data++;
        for (uint8_t i = 8; i; i--) {
            uint8_t sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum) crc ^= 0x8C;
            extract >>= 1;
        }
    }
    return crc;
}

// ----------------------------------------------------------------------------
//  Packet queue (passes), shared with the I2C interrupt
// ----------------------------------------------------------------------------
Packet queue[QUEUE_SIZE];
volatile uint8_t qHead = 0, qCount = 0;

bool queuePush(const Packet &p) {
    bool ok = false;
    noInterrupts();
    if (qCount < QUEUE_SIZE) {
        queue[(qHead + qCount) % QUEUE_SIZE] = p;
        qCount++;
        ok = true;
    }
    interrupts();
    return ok;                          // full: keep the older passes, the new one is dropped
}

bool queuePop(Packet &p) {              // also called from the I2C interrupt
    uint8_t sreg = SREG;
    noInterrupts();
    bool ok = qCount > 0;
    if (ok) {
        p = queue[qHead];
        qHead = (qHead + 1) % QUEUE_SIZE;
        qCount--;
    }
    SREG = sreg;
    return ok;
}

void queueClear() {
    noInterrupts();
    qHead = qCount = 0;
    interrupts();
}

void put32(uint8_t *p, uint32_t v) {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Next 13-byte packet for the controller: a queued pass, else a time stamp / status in turn.
void nextPacket(uint8_t *out) {
    Packet p;
    if (queuePop(p)) {
        memcpy(out, p.b, PACKET_SIZE);
        return;
    }
    static bool timeTurn = false;
    timeTurn = !timeTurn;
    memset(out, 0, PACKET_SIZE);
    out[1] = address;
    if (timeTurn && state == RACE && !decoderSilent()) {
        out[0] = PKT_TIME;
        put32(out + 3, decoderTime);
        out[2] = crc8(out + 3, 4);
        return;
    }
    out[0] = PKT_STATUS;
    out[3] = state;
    out[4] = (uint8_t)clockDelta;
    out[5] = (uint8_t)min(255, Serial.available() * 4);    // decoder UART buffer use (64 bytes -> 0..255)
    out[6] = worstLoop;
    out[7] = errDecoder;
    out[8] = errQueue;
    out[9] = errLink;
    out[10] = qCount;
    out[11] = FW_VERSION;
    out[12] = (decoderSilent() ? 1 : 0) | (state == RACE && linkLost() ? 2 : 0);
    out[2] = crc8(out + 3, 10);
    worstLoop = 0;
}

// ----------------------------------------------------------------------------
//  Decoder (ATmega16, Robitronic protocol on the hardware UART)
//    frame: length (incl. itself) | checksum | type | data...
//    0x84 car (13 bytes): ID[4] LE, time ms[4] LE, hits, strength
//    0x83 time stamp (11 bytes): time ms[4] LE, 4 unknown bytes
// ----------------------------------------------------------------------------
void decoderCommand(bool start) {
    static const uint8_t startFrame[] = {0x03, 0xB9, 0x01};   // start: decoder clock from 0, detect cars
    static const uint8_t resetFrame[] = {0x03, 0xB0, 0x02};   // reset / idle
    Serial.write(start ? startFrame : resetFrame, 3);
}

uint8_t dBuf[16];
uint8_t dLen = 0, dPos = 0;
uint32_t dLastByteMs = 0;

void handleDecoderFrame() {
    if (dBuf[2] == PKT_ID && dLen == 13) {
        if (state != RACE) return;                     // not timing: ignore
        Packet p;
        p.b[0] = PKT_ID;
        p.b[1] = address;
        memcpy(p.b + 3, dBuf + 3, 10);                 // ID, time, hits, strength
        p.b[2] = crc8(p.b + 3, 10);
        if (!queuePush(p)) INC(errQueue);
        flashUntil = millis() + 60;
    } else if (dBuf[2] == PKT_TIME && dLen >= 7) {
        decoderTime = get32(dBuf + 3);
        if (state == RACE) {
            long d = (long)(millis() - raceStartMs) - (long)decoderTime;
            clockDelta = d > 127 ? 127 : d < -127 ? -127 : (int8_t)d;
        }
    }
    // other frame types (status, echoes): ignored
}

void decoderLoop() {
    if (dPos && millis() - dLastByteMs > FRAME_GAP_MS) {   // truncated frame: drop it, resynchronise
        INC(errDecoder);
        dPos = 0;
    }
    while (Serial.available()) {
        uint8_t c = Serial.read();
        dLastByteMs = lastDecoderMs = millis();
        if (dPos == 0) {
            if (c < 3 || c > sizeof(dBuf)) {               // not a plausible length byte: skip
                INC(errDecoder);
                continue;
            }
            dLen = c;
        }
        dBuf[dPos++] = c;
        if (dPos == dLen) {
            handleDecoderFrame();
            dPos = 0;
        }
    }
}

// ----------------------------------------------------------------------------
//  Race state
// ----------------------------------------------------------------------------
void startRace() {
    queueClear();
    errDecoder = errQueue = errLink = 0;
    decoderCommand(true);
    raceStartMs = lastDecoderMs = millis();
    decoderTime = 0;
    clockDelta = 0;
    state = RACE;
}

void stopRace() {
    decoderCommand(false);
    queueClear();
    lastKeepaliveMs = millis();
    state = CONNECTED;
}

volatile uint8_t pendingCmd = 0;        // I2C: command received in the interrupt, run in loop()

void command(uint8_t cmd) {
    if (cmd == CMD_START) startRace();
    else if (cmd == CMD_STOP) stopRace();
}

void stateLoop() {
    uint8_t cmd = pendingCmd;
    if (cmd) {
        pendingCmd = 0;
        command(cmd);
    }
    // a lost link during a race does NOT stop the decoder: its clock must keep running (v1 restarted it)
    if (state == DISCONNECTED && !linkLost()) {
        state = CONNECTED;
        decoderCommand(false);
        lastKeepaliveMs = millis();
    } else if (state == CONNECTED && linkLost()) {
        state = DISCONNECTED;
    }
    if (state != RACE && millis() - lastKeepaliveMs > KEEPALIVE_MS) {
        lastKeepaliveMs = millis();
        decoderCommand(false);
    }
}

// ----------------------------------------------------------------------------
//  Link: RS485
// ----------------------------------------------------------------------------
#if LINK_RS485
uint8_t lBuf[4];
uint8_t lPos = 0;
uint32_t lLastByteMs = 0;
uint8_t reply[4 + PACKET_SIZE];
bool haveReply = false;

void handleRequest(uint8_t addr, uint8_t cmd) {
    if (addr != address && addr != 0) return;
    lastLinkMs = millis();
    if (cmd == CMD_START || cmd == CMD_STOP) {
        command(cmd);
        return;
    }
    if (addr == 0 || (cmd != CMD_POLL && cmd != CMD_REPEAT)) return;   // nobody answers a broadcast
    if (cmd == CMD_POLL || !haveReply) {
        reply[0] = REP_SYNC;
        reply[1] = address;
        reply[2] = PACKET_SIZE;
        nextPacket(reply + 3);
        reply[3 + PACKET_SIZE] = crc8(reply + 1, 2 + PACKET_SIZE);
        haveReply = true;
    } else {
        INC(errLink);                                  // controller lost our previous reply
    }
    delayMicroseconds(REPLY_DELAY_US);
    rs485.write(reply, sizeof(reply));
}

void linkLoop() {
    if (lPos && millis() - lLastByteMs > FRAME_GAP_MS) lPos = 0;
    while (rs485.available()) {
        uint8_t c = rs485.read();
        lLastByteMs = lastBusByteMs = millis();
        if (lPos == 0 && c != REQ_SYNC) continue;      // wait for a request start (skips echoes, replies, noise)
        lBuf[lPos++] = c;
        if (lPos == 4) {
            lPos = 0;
            if (crc8(lBuf + 1, 2) != lBuf[3]) {
                INC(errLink);
                continue;
            }
            lastBusFrameMs = millis();
            handleRequest(lBuf[1], lBuf[2]);
        }
    }
}

// "hello" at power-up: one reply frame (status packet) sent unasked, so the controller can check
// this gate's transmit path even when its receive path is broken. Delayed by the address so that
// gates powered together do not talk at the same time.
void sendHello() {
    delay(20 + (address - BASE_ADDRESS) * 15);
    reply[0] = REP_SYNC;
    reply[1] = address;
    reply[2] = PACKET_SIZE;
    nextPacket(reply + 3);
    reply[3 + PACKET_SIZE] = crc8(reply + 1, 2 + PACKET_SIZE);
    rs485.write(reply, sizeof(reply));
}

void linkBegin() {
    rs485.begin(RS485_BAUD);
    sendHello();
}

// ----------------------------------------------------------------------------
//  Link: I2C slave (legacy)
// ----------------------------------------------------------------------------
#else
void onI2CRequest() {                   // interrupt: answer at once
    uint8_t out[PACKET_SIZE];
    nextPacket(out);
    Wire.write(out, PACKET_SIZE);
    lastLinkMs = millis();
}

void onI2CReceive(int n) {              // interrupt: keep the command for loop()
    uint8_t cmd = n > 0 ? Wire.read() : 0;
    while (Wire.available()) Wire.read();
    if (cmd == CMD_START || cmd == CMD_STOP) pendingCmd = cmd;
    lastLinkMs = millis();
}

void linkLoop() {}

void linkBegin() {
    Wire.begin(address);
    Wire.onRequest(onI2CRequest);
    Wire.onReceive(onI2CReceive);
}
#endif

// ----------------------------------------------------------------------------
//  LED (also shown in the README)
//    power-up          N slow blinks = gate number (1 = address 21 ... 8 = 28)
//    fast blink 5 Hz   nothing received from the bus for 2 s
//    double flash      bytes or requests heard, but none valid for this gate
//                      (other address, wrong speed, noise, A/B swapped)
//    blink 1 Hz        ping/pong with the controller OK
//    on, short gaps    race running
//    4 Hz              race running but decoder silent or controller lost
//    short inversion   a car was detected
// ----------------------------------------------------------------------------
bool recent(uint32_t t) { return millis() - t < LINK_TIMEOUT_MS; }

void led(bool on) { digitalWrite(LED_PIN, (on != (LED_ACTIVE_LOW != 0)) ? HIGH : LOW); }

void ledLoop() {
    uint32_t now = millis();
    bool level;
    if (state == RACE && (decoderSilent() || linkLost())) level = (now % 250) < 60;
    else if (state == RACE) level = (now % 2000) < 1900;
    else if (!linkLost()) level = (now % 1000) < 500;                 // ping/pong OK: 1 Hz
    else if (recent(lastBusByteMs) || recent(lastBusFrameMs)) {        // hears something, not for us
        uint16_t p = now % 1200;
        level = p < 100 || (p >= 250 && p < 350);
    }
    else level = (now % 200) < 100;                                    // silence: fast blink
    if (now < flashUntil) level = !level;
    led(level);
}

void blinkAddress() {
    for (uint8_t i = 0; i <= address - BASE_ADDRESS; i++) {
        led(true); delay(250);
        led(false); delay(250);
    }
    delay(700);
}

// ----------------------------------------------------------------------------
//  Setup / loop
// ----------------------------------------------------------------------------
uint8_t readAddress() {
    pinMode(ID_PIN0, INPUT_PULLUP);
    pinMode(ID_PIN1, INPUT_PULLUP);
    pinMode(ID_PIN2, INPUT_PULLUP);
    delay(2);
    uint8_t id = (digitalRead(ID_PIN0) == LOW ? 1 : 0)
               | (digitalRead(ID_PIN1) == LOW ? 2 : 0)
               | (digitalRead(ID_PIN2) == LOW ? 4 : 0);
    return BASE_ADDRESS + id;           // 21..28
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    led(false);
    address = readAddress();
    blinkAddress();
    Serial.begin(DECODER_BAUD);
    decoderCommand(false);
    lastKeepaliveMs = millis();
    lastLinkMs = millis() - LINK_TIMEOUT_MS - 1;   // start DISCONNECTED
    linkBegin();
}

void loop() {
    uint32_t t0 = micros();
    linkLoop();
    decoderLoop();
    stateLoop();
    ledLoop();
    uint32_t tenths = (micros() - t0) / 100;
    if (tenths > worstLoop) worstLoop = tenths > 255 ? 255 : tenths;
}
