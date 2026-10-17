"""RS485 framing between the controller (master) and the gates (slaves).

Half-duplex, master polls, 8N1, 57600 baud by default (receivers use AltSoftSerial). The payload is the very same
13-byte packet as over I2C (0x82 / 0x83 / 0x84), so ricino.core does not change.

  request  master -> gate : A5 | addr | cmd | crc8(addr, cmd)                      4 bytes
  reply    gate -> master : 5A | addr | 0D  | packet[13] | crc8(addr, 0D, packet)  17 bytes

  cmd 01 = poll (the gate answers its next packet)
      02 = repeat (the last reply was lost or corrupted: send the same one again)
      8A = start (gate clock = 0, decode IR)      8F = stop
  addr 0 = broadcast (every gate executes, nobody answers): one start frame starts all gates
  at the same instant.

A gate only answers a poll addressed to it. Bytes before the sync byte are ignored, so an echo
of the request (some transceivers) or line noise does not matter.

CRC-8: Dallas/Maxim (reflected 0x8C), the same as the 0x84 packet.

Pure functions, CPython / MicroPython common subset (used by ricino.esp32 and the tests,
and the reference for the receiver firmware).
"""
from ricino.core import crc8, PACKET_SIZE

REQ_SYNC = 0xA5
REP_SYNC = 0x5A
POLL = 0x01
REPEAT = 0x02
BROADCAST = 0
REPLY_SIZE = 4 + PACKET_SIZE


def request(addr, cmd):
    return bytes([REQ_SYNC, addr, cmd, crc8(bytes([addr, cmd]))])


def reply(addr, packet):
    """Gate side (reference / tests)."""
    body = bytes([addr, PACKET_SIZE]) + bytes(packet[:PACKET_SIZE]) + bytes(PACKET_SIZE - len(packet[:PACKET_SIZE]))
    return bytes([REP_SYNC]) + body + bytes([crc8(body)])


def parse_request(data):
    """Gate side: (addr, cmd) of the first valid request in data, or None."""
    i = 0
    while i + 4 <= len(data):
        if data[i] == REQ_SYNC and crc8(data[i + 1: i + 3]) == data[i + 3]:
            return data[i + 1], data[i + 2]
        i += 1
    return None


def parse_reply(data, addr):
    """Master side: the 13-byte packet of a valid reply from addr, b"" if incomplete, None if invalid."""
    i = 0
    while i < len(data):
        if data[i] == REP_SYNC:
            if len(data) - i < REPLY_SIZE:
                return b""                          # wait for more bytes
            frame = data[i: i + REPLY_SIZE]
            if frame[1] == addr and frame[2] == PACKET_SIZE and crc8(frame[1: REPLY_SIZE - 1]) == frame[REPLY_SIZE - 1]:
                return bytes(frame[3: 3 + PACKET_SIZE])
        i += 1
    return b"" if len(data) < REPLY_SIZE * 2 else None
