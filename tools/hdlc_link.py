"""HDLC control/reply frames; asset bytes follow a block header raw."""
import time
from collections import deque

REQUEST, REPLY = 0xF0, 0xF1
CONTROL_MAX = 10


def encode(sequence, kind, payload):
    if len(payload) > 255:
        raise ValueError("HDLC payload exceeds the 8-bit length")
    raw = bytes([sequence & 255, kind, len(payload)]) + bytes(payload)
    raw += bytes([(~sum(raw)) & 255])
    wire = bytearray([0x7E])
    for byte in raw:
        if byte in (0x7D, 0x7E):
            wire.extend((0x7D, byte ^ 0x20))
        else:
            wire.append(byte)
    wire.append(0x7E)
    return bytes(wire)


class Decoder:
    """Incremental counterpart of the imported C HdlcParser HSM."""
    def __init__(self):
        self.active = self.escaped = False
        self.frame = bytearray()

    def feed(self, wire):
        frames = []
        for byte in wire:
            if byte == 0x7E:
                self.active, self.escaped = True, False
                self.frame.clear()
                continue
            if not self.active:
                continue
            if byte == 0x7D:
                if self.escaped:
                    self.active = self.escaped = False
                    self.frame.clear()
                else:
                    self.escaped = True
                continue
            if self.escaped:
                byte ^= 0x20
                self.escaped = False
            self.frame.append(byte)
            if len(self.frame) >= 3 and len(self.frame) == 4 + self.frame[2]:
                if sum(self.frame) & 255 == 255:
                    frames.append((self.frame[0], self.frame[1],
                                   bytes(self.frame[3:-1])))
                self.frame.clear()
        return frames


class AssetLink:
    def __init__(self, serial, trace=None):
        self.serial, self.trace = serial, trace
        self.decoder, self.pending = Decoder(), deque()
        self.sequence = 0

    def send(self, packet, data=b""):
        if not 0 < len(packet) <= CONTROL_MAX:
            raise ValueError("control packet length out of bounds")
        wire = encode(self.sequence, REQUEST, packet) + bytes(data)
        if self.serial.write(wire) != len(wire):
            raise OSError("short UART write")
        self.sequence = (self.sequence + 1) & 255

    def receive(self, request_kind, index, timeout):
        deadline = time.monotonic() + timeout
        while True:
            while self.pending:
                sequence, kind, payload = self.pending.popleft()
                if kind != REPLY:
                    if self.trace is not None:
                        self.trace(sequence, kind, payload)
                    continue
                if len(payload) < 4:
                    continue
                if (payload[0] == request_kind
                        and int.from_bytes(payload[1:3], "big") == index):
                    return payload[3:]
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            self.serial.timeout = min(remaining, 0.05)
            self.pending.extend(self.decoder.feed(self.serial.read(256)))
