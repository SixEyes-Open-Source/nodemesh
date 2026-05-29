#!/usr/bin/env python3
"""
test_fault_injection.py — Fault-injection tests for NodeMesh IL node0.

Simulates error conditions that the firmware must handle correctly:
  1. UART framing / resync (garbage bytes, split packets, overlapping frames)
  2. Cam vision staleness (mergeIntoPacket drops stale vision features)
  3. Heartbeat / packet timeout detection (gap detection for safety)
  4. Motor safety gate (no motion command if no fresh packets)

These tests run entirely on the host — no hardware required.

Run with:
  py tools/test_fault_injection.py
  py tools/test_fault_injection.py -v

Exit code 0 = all tests passed.
"""

import struct
import sys
import traceback
from typing import List, Optional, Tuple

# ─────────────────────────────────────────────────────────────────────────────
# Constants (keep in sync with firmware headers)
# ─────────────────────────────────────────────────────────────────────────────

PACKET_MAGIC    = 0x4E4D5650
PACKET_VERSION  = 1
JOINT_COUNT     = 6
VISION_BYTES    = 128
PACKET_FMT      = "<IBBHII6f128BH"
PACKET_SIZE     = struct.calcsize(PACKET_FMT)

VISION_STALE_MS = 300    # calib::kVisionStaleMs
HEARTBEAT_TIMEOUT_MS = 500  # documented safety timeout

assert PACKET_SIZE == 170


# ─────────────────────────────────────────────────────────────────────────────
# CRC / packet helpers (shared with test_packets.py)
# ─────────────────────────────────────────────────────────────────────────────

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def build_packet(source_node: int, timestamp_us: int, seq: int,
                 joints: List[float], vision: bytes) -> bytes:
    assert len(joints) == JOINT_COUNT and len(vision) == VISION_BYTES
    raw = struct.pack(PACKET_FMT, PACKET_MAGIC, PACKET_VERSION, source_node,
                      PACKET_SIZE, timestamp_us, seq, *joints, *vision, 0)
    return raw[:-2] + struct.pack("<H", crc16_ccitt(raw))


def validate_packet(raw: bytes) -> bool:
    if len(raw) != PACKET_SIZE:
        return False
    f = struct.unpack(PACKET_FMT, raw)
    if f[0] != PACKET_MAGIC or f[1] != PACKET_VERSION:
        return False
    stored = f[-1]
    return crc16_ccitt(raw[:-2] + b"\x00\x00") == stored


def default_packet(source_node=1, seq=1, ts=1000,
                   joints=None, vision=None) -> bytes:
    j = joints or [0.0] * JOINT_COUNT
    v = vision or bytes(VISION_BYTES)
    return build_packet(source_node, ts, seq, j, v)


# ─────────────────────────────────────────────────────────────────────────────
# UART framing model
# Mirrors UartInput::popPacket() — scan for magic, validate, consume.
# ─────────────────────────────────────────────────────────────────────────────

class UartFramer:
    """
    Pure-Python model of UartInput ring buffer + packet parser.
    Mirrors the firmware logic:
      - scan from start for PACKET_MAGIC (4 bytes LE)
      - validate full packet when enough bytes available
      - consume on success, advance one byte on failure
    """
    MAGIC_BYTES = struct.pack("<I", PACKET_MAGIC)
    RX_BUF_MAX = PACKET_SIZE * 4

    def __init__(self):
        self.buf = bytearray()
        self.discarded = 0   # bytes thrown away during resync

    def feed(self, data: bytes):
        self.buf.extend(data)
        # Trim if buffer overflows (mirror firmware: keep last PACKET_SIZE-1 bytes)
        if len(self.buf) > self.RX_BUF_MAX:
            keep = PACKET_SIZE - 1
            self.discarded += len(self.buf) - keep
            self.buf = self.buf[-keep:]

    def pop(self) -> Optional[bytes]:
        """Try to extract one valid packet. Returns raw bytes or None."""
        while len(self.buf) >= PACKET_SIZE:
            idx = self.buf.find(self.MAGIC_BYTES)
            if idx == -1:
                # No magic anywhere — discard all but last 3 bytes (partial magic)
                self.discarded += len(self.buf) - 3
                self.buf = self.buf[-3:]
                return None
            if idx > 0:
                self.discarded += idx
                del self.buf[:idx]
            # buf[0] is now the magic
            if len(self.buf) < PACKET_SIZE:
                return None
            candidate = bytes(self.buf[:PACKET_SIZE])
            if validate_packet(candidate):
                del self.buf[:PACKET_SIZE]
                return candidate
            # Magic matched but packet invalid — skip one byte and keep scanning
            self.discarded += 1
            del self.buf[:1]
        return None

    def pop_all(self) -> List[bytes]:
        packets = []
        while True:
            p = self.pop()
            if p is None:
                break
            packets.append(p)
        return packets


# ─────────────────────────────────────────────────────────────────────────────
# Vision merge model
# Mirrors CamInput::mergeIntoPacket() staleness logic.
# ─────────────────────────────────────────────────────────────────────────────

def merge_vision(base_packet_raw: bytes,
                 n2_vision: Optional[bytes], n2_age_ms: int,
                 n3_vision: Optional[bytes], n3_age_ms: int,
                 stale_ms: int = VISION_STALE_MS) -> bytes:
    """
    Python model of CamInput::mergeIntoPacket().
    Returns the merged packet (vision features updated in-place if fresh).
    n2_vision / n3_vision: 128-byte arrays or None if never received.
    n2_age_ms / n3_age_ms: ms since last packet received from that node.
    """
    f = list(struct.unpack(PACKET_FMT, base_packet_raw))
    # vision_features are fields[12..139] in the unpacked tuple
    base_vision = list(f[12:140])

    use_n2 = n2_vision is not None and n2_age_ms <= stale_ms
    use_n3 = n3_vision is not None and n3_age_ms <= stale_ms

    if use_n2 and use_n3:
        merged = [(a + b) // 2 for a, b in zip(n2_vision, n3_vision)]
    elif use_n2:
        merged = list(n2_vision)
    elif use_n3:
        merged = list(n3_vision)
    else:
        merged = base_vision  # no fresh cam data — leave unchanged

    new_f = f[:12] + merged + f[140:]
    raw = struct.pack(PACKET_FMT, *new_f)
    return raw


# ─────────────────────────────────────────────────────────────────────────────
# Heartbeat / safety model
# ─────────────────────────────────────────────────────────────────────────────

class HeartbeatMonitor:
    """
    Models the firmware safety contract:
    - Motors may only actuate when a fresh packet was received within
      HEARTBEAT_TIMEOUT_MS.
    - If the gap since last packet exceeds the threshold, motors must be
      disabled (EN de-asserted).
    """
    def __init__(self, timeout_ms: int = HEARTBEAT_TIMEOUT_MS):
        self.timeout_ms = timeout_ms
        self.last_packet_ms: Optional[int] = None

    def on_packet(self, now_ms: int):
        self.last_packet_ms = now_ms

    def motors_enabled(self, now_ms: int) -> bool:
        if self.last_packet_ms is None:
            return False
        return (now_ms - self.last_packet_ms) < self.timeout_ms

    def ms_since_last(self, now_ms: int) -> Optional[int]:
        if self.last_packet_ms is None:
            return None
        return now_ms - self.last_packet_ms


# ─────────────────────────────────────────────────────────────────────────────
# Test runner
# ─────────────────────────────────────────────────────────────────────────────

_results = []
_verbose = "-v" in sys.argv


def test(name):
    def decorator(fn):
        try:
            fn()
            _results.append((name, True, None))
            if _verbose:
                print(f"  PASS  {name}")
        except AssertionError as e:
            _results.append((name, False, str(e)))
            if _verbose:
                print(f"  FAIL  {name}: {e}")
        except Exception as e:
            _results.append((name, False, traceback.format_exc()))
            if _verbose:
                print(f"  ERR   {name}: {e}")
        return fn
    return decorator


# ─────────────────────────────────────────────────────────────────────────────
# ── UART framing tests ─────────────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

@test("clean packet parses immediately")
def _():
    f = UartFramer()
    pkt = default_packet()
    f.feed(pkt)
    parsed = f.pop_all()
    assert len(parsed) == 1
    assert validate_packet(parsed[0])

@test("packet preceded by garbage bytes is recovered")
def _():
    f = UartFramer()
    garbage = bytes([0xAA, 0xBB, 0xCC, 0x00, 0x11, 0xFF] * 5)
    pkt = default_packet(seq=1)
    f.feed(garbage + pkt)
    parsed = f.pop_all()
    assert len(parsed) == 1, f"Expected 1 packet, got {len(parsed)}"
    assert validate_packet(parsed[0])
    assert f.discarded == len(garbage)

@test("packet followed by garbage still parses")
def _():
    f = UartFramer()
    pkt = default_packet(seq=1)
    garbage = bytes([0xDE, 0xAD] * 10)
    f.feed(pkt + garbage)
    parsed = f.pop_all()
    assert len(parsed) == 1

@test("two back-to-back packets both parse")
def _():
    f = UartFramer()
    p1 = default_packet(seq=1, ts=1000)
    p2 = default_packet(seq=2, ts=2000)
    f.feed(p1 + p2)
    parsed = f.pop_all()
    assert len(parsed) == 2
    for p in parsed:
        assert validate_packet(p)

@test("packet split across two feed() calls is reassembled")
def _():
    f = UartFramer()
    pkt = default_packet(seq=1)
    split = len(pkt) // 2
    f.feed(pkt[:split])
    assert f.pop() is None, "Should not have a packet yet"
    f.feed(pkt[split:])
    parsed = f.pop_all()
    assert len(parsed) == 1
    assert validate_packet(parsed[0])

@test("packet split into byte-by-byte feeds is reassembled")
def _():
    f = UartFramer()
    pkt = default_packet(seq=1)
    for b in pkt:
        f.feed(bytes([b]))
    parsed = f.pop_all()
    assert len(parsed) == 1
    assert validate_packet(parsed[0])

@test("corrupted packet in stream is skipped, next valid packet recovered")
def _():
    f = UartFramer()
    bad = bytearray(default_packet(seq=1))
    bad[10] ^= 0x01   # corrupt data field → CRC fails
    good = default_packet(seq=2, ts=5000)
    f.feed(bytes(bad) + good)
    parsed = f.pop_all()
    assert len(parsed) == 1, f"Expected 1 (good) packet, got {len(parsed)}"
    fields = struct.unpack(PACKET_FMT, parsed[0])
    assert fields[5] == 2, "Should have recovered the good packet (seq=2)"

@test("magic bytes appearing in payload don't cause false parse")
def _():
    # Embed the magic sequence inside the vision field of a packet, followed
    # by a real valid packet. Only the real packet should be extracted.
    magic_bytes = struct.pack("<I", PACKET_MAGIC)
    vision_with_magic = magic_bytes + bytes(VISION_BYTES - 4)
    pkt = default_packet(seq=1, vision=vision_with_magic)
    good = default_packet(seq=2, ts=9000)
    f = UartFramer()
    f.feed(pkt + good)
    parsed = f.pop_all()
    assert len(parsed) == 2, f"Expected 2 packets, got {len(parsed)}"

@test("50 sequential packets all recovered from contiguous stream")
def _():
    f = UartFramer()
    parsed = []
    for i in range(1, 51):
        f.feed(default_packet(seq=i, ts=i * 2500))
        parsed.extend(f.pop_all())
    assert len(parsed) == 50, f"Expected 50, got {len(parsed)}"
    seqs = [struct.unpack(PACKET_FMT, p)[5] for p in parsed]
    assert seqs == list(range(1, 51))

@test("100 bytes of random-looking garbage produces no false packets")
def _():
    # Use a deterministic pattern that doesn't accidentally contain valid magic+CRC
    f = UartFramer()
    data = bytes([(i * 31 + 7) % 256 for i in range(100)])
    f.feed(data)
    parsed = f.pop_all()
    assert len(parsed) == 0, f"Got {len(parsed)} false packet(s) from garbage"

@test("partial magic at buffer boundary doesn't lose next packet")
def _():
    # Feed first 3 bytes of magic, then the real packet
    magic_start = struct.pack("<I", PACKET_MAGIC)[:3]
    good = default_packet(seq=5)
    f = UartFramer()
    f.feed(magic_start)
    f.feed(good)
    parsed = f.pop_all()
    assert len(parsed) == 1
    assert validate_packet(parsed[0])


# ─────────────────────────────────────────────────────────────────────────────
# ── Cam vision staleness tests ─────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

N2_VISION = bytes([100] * 64 + [0] * 64)   # node2 brightness pattern
N3_VISION = bytes([200] * 64 + [0] * 64)   # node3 brightness pattern
ZERO_VISION = bytes(VISION_BYTES)


@test("fresh vision from both cams is averaged into packet")
def _():
    base = default_packet(source_node=1)
    result = merge_vision(base, N2_VISION, 0, N3_VISION, 0)
    f = struct.unpack(PACKET_FMT, result)
    vision = f[12:140]
    # Expected: (100+200)//2 = 150 for indices 0-63, 0 for 64-127
    for i in range(64):
        assert vision[i] == 150, f"vision[{i}] = {vision[i]}, expected 150"
    for i in range(64, 128):
        assert vision[i] == 0

@test("only node2 fresh: node2 vision used alone")
def _():
    base = default_packet(source_node=1)
    result = merge_vision(base, N2_VISION, 0, N3_VISION, VISION_STALE_MS + 1)
    f = struct.unpack(PACKET_FMT, result)
    vision = f[12:76]
    for i, v in enumerate(vision):
        assert v == 100, f"vision[{i}] = {v}, expected 100"

@test("only node3 fresh: node3 vision used alone")
def _():
    base = default_packet(source_node=1)
    result = merge_vision(base, N2_VISION, VISION_STALE_MS + 1, N3_VISION, 0)
    f = struct.unpack(PACKET_FMT, result)
    vision = f[12:76]
    for i, v in enumerate(vision):
        assert v == 200, f"vision[{i}] = {v}, expected 200"

@test("both cams stale: original packet vision preserved unchanged")
def _():
    original_vision = bytes([42] * 64 + [7] * 64)
    base = default_packet(source_node=1, vision=original_vision)
    result = merge_vision(base,
                          N2_VISION, VISION_STALE_MS + 1,
                          N3_VISION, VISION_STALE_MS + 1)
    f = struct.unpack(PACKET_FMT, result)
    vision = bytes(f[12:140])
    assert vision == original_vision, "Stale cams should not overwrite existing vision"

@test("no cam data ever received: original packet vision preserved")
def _():
    original_vision = bytes([77] * VISION_BYTES)
    base = default_packet(source_node=1, vision=original_vision)
    result = merge_vision(base, None, 0, None, 0)
    f = struct.unpack(PACKET_FMT, result)
    vision = bytes(f[12:140])
    assert vision == original_vision

@test("cam exactly at stale boundary is still fresh (<=, not <)")
def _():
    base = default_packet(source_node=1)
    # age == VISION_STALE_MS exactly should still be used
    result = merge_vision(base, N2_VISION, VISION_STALE_MS, N3_VISION, VISION_STALE_MS)
    f = struct.unpack(PACKET_FMT, result)
    vision = f[12:76]
    for i, v in enumerate(vision):
        assert v == 150, f"vision[{i}] = {v} at exact boundary"

@test("cam one ms past stale boundary is dropped")
def _():
    base = default_packet(source_node=1)
    original_vision = bytes(VISION_BYTES)
    base_with_zero = build_packet(1, 0, 1, [0.0]*6, original_vision)
    result = merge_vision(base_with_zero,
                          N2_VISION, VISION_STALE_MS + 1,
                          N3_VISION, VISION_STALE_MS + 1)
    f = struct.unpack(PACKET_FMT, result)
    vision = bytes(f[12:140])
    assert vision == original_vision

@test("vision averaging is symmetric: swap n2/n3 gives same result")
def _():
    base = default_packet(source_node=1)
    r1 = merge_vision(base, N2_VISION, 0, N3_VISION, 0)
    r2 = merge_vision(base, N3_VISION, 0, N2_VISION, 0)
    f1 = struct.unpack(PACKET_FMT, r1)[12:140]
    f2 = struct.unpack(PACKET_FMT, r2)[12:140]
    assert f1 == f2, "Average should be symmetric"

@test("cam vision staleness resets correctly on new packet arrival")
def _():
    # Simulate: node2 was stale, then a fresh packet arrives (age resets to 0)
    base = default_packet(source_node=1)
    # First call: stale
    r_stale = merge_vision(base, N2_VISION, VISION_STALE_MS + 100, None, 0)
    f_stale = struct.unpack(PACKET_FMT, r_stale)[12:76]
    # Second call: fresh after reset
    r_fresh = merge_vision(base, N2_VISION, 0, None, 0)
    f_fresh = struct.unpack(PACKET_FMT, r_fresh)[12:76]
    assert f_stale != f_fresh, "Fresh arrival should change vision output"
    for v in f_fresh:
        assert v == 100


# ─────────────────────────────────────────────────────────────────────────────
# ── Heartbeat / safety tests ───────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

@test("motors disabled before any packet received")
def _():
    hb = HeartbeatMonitor()
    assert not hb.motors_enabled(0)
    assert not hb.motors_enabled(1000)

@test("motors enabled immediately after first packet")
def _():
    hb = HeartbeatMonitor()
    hb.on_packet(now_ms=1000)
    assert hb.motors_enabled(1000)

@test("motors stay enabled within timeout window")
def _():
    hb = HeartbeatMonitor()
    hb.on_packet(now_ms=0)
    for t in range(0, HEARTBEAT_TIMEOUT_MS, 10):
        assert hb.motors_enabled(t), f"Should be enabled at t={t}"

@test("motors disabled exactly at timeout boundary")
def _():
    hb = HeartbeatMonitor()
    hb.on_packet(now_ms=0)
    assert not hb.motors_enabled(HEARTBEAT_TIMEOUT_MS)

@test("motors disabled after timeout elapsed")
def _():
    hb = HeartbeatMonitor()
    hb.on_packet(now_ms=0)
    assert not hb.motors_enabled(HEARTBEAT_TIMEOUT_MS + 1)
    assert not hb.motors_enabled(HEARTBEAT_TIMEOUT_MS + 5000)

@test("motor re-enabled after timeout if new packet arrives")
def _():
    hb = HeartbeatMonitor()
    hb.on_packet(now_ms=0)
    assert not hb.motors_enabled(HEARTBEAT_TIMEOUT_MS + 100)   # timed out
    hb.on_packet(now_ms=HEARTBEAT_TIMEOUT_MS + 100)            # new packet
    assert hb.motors_enabled(HEARTBEAT_TIMEOUT_MS + 100)       # re-enabled

@test("rapid packet stream: motors remain continuously enabled")
def _():
    hb = HeartbeatMonitor()
    # Simulate 400 Hz for 2 seconds (800 packets)
    interval_ms = 1000 // 400   # 2 ms
    for i in range(800):
        now = i * interval_ms
        if i % 10 == 0:          # packet every 20 ms (well within timeout)
            hb.on_packet(now)
        if i > 0:
            assert hb.motors_enabled(now), f"Should be enabled at tick {i}"

@test("single packet gap of 499 ms: motors still enabled")
def _():
    hb = HeartbeatMonitor()
    hb.on_packet(now_ms=0)
    assert hb.motors_enabled(HEARTBEAT_TIMEOUT_MS - 1)

@test("gap detection: 600 ms silence triggers disable")
def _():
    hb = HeartbeatMonitor()
    hb.on_packet(now_ms=0)
    hb.on_packet(now_ms=100)
    hb.on_packet(now_ms=200)
    # Now silence for 600 ms
    assert not hb.motors_enabled(800)

@test("ms_since_last is None before any packet")
def _():
    hb = HeartbeatMonitor()
    assert hb.ms_since_last(1000) is None

@test("ms_since_last returns correct elapsed time")
def _():
    hb = HeartbeatMonitor()
    hb.on_packet(now_ms=1000)
    assert hb.ms_since_last(1250) == 250
    assert hb.ms_since_last(2000) == 1000

@test("safety: corrupt packet stream gives no false heartbeats")
def _():
    # Even if garbage arrives on the UART, motors should stay disabled
    # because no valid packet is ever extracted.
    hb = HeartbeatMonitor()
    f = UartFramer()
    garbage = bytes([(i * 53 + 13) % 256 for i in range(500)])
    f.feed(garbage)
    parsed = f.pop_all()
    # Simulate firmware: only call hb.on_packet() if a valid packet is extracted
    for p in parsed:
        hb.on_packet(now_ms=100)
    assert not hb.motors_enabled(100), \
        "Corrupt stream must not trigger heartbeat"

@test("combined fault: stale vision + heartbeat timeout disables everything")
def _():
    hb = HeartbeatMonitor()
    hb.on_packet(now_ms=0)
    now = HEARTBEAT_TIMEOUT_MS + 200
    motors_ok = hb.motors_enabled(now)
    base = default_packet()
    result = merge_vision(base, N2_VISION, VISION_STALE_MS + 1,
                          N3_VISION, VISION_STALE_MS + 1)
    f = struct.unpack(PACKET_FMT, result)
    vision = bytes(f[12:140])
    assert not motors_ok, "Motors should be disabled after timeout"
    assert vision == bytes(VISION_BYTES), "Vision should be zeroed (original was zeros)"


# ─────────────────────────────────────────────────────────────────────────────
# ── Results
# ─────────────────────────────────────────────────────────────────────────────

def main():
    passed = [r for r in _results if r[1]]
    failed = [r for r in _results if not r[1]]

    print(f"\nNodeMesh IL Fault-Injection Tests — {len(passed)}/{len(_results)} passed")
    if failed:
        print(f"\nFAILED ({len(failed)}):")
        for name, _, msg in failed:
            print(f"  ✗ {name}")
            if msg:
                for line in msg.splitlines():
                    print(f"      {line}")
    else:
        print("All tests passed.")

    sys.exit(0 if not failed else 1)


if __name__ == "__main__":
    main()
