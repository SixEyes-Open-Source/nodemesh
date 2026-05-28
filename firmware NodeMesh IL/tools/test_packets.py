#!/usr/bin/env python3
"""
test_packets.py — Host-side packet-level integration tests for NodeMesh IL.

Exercises the binary codec, CRC, sequence continuity, SessionHeader layout,
and per-node packet construction logic — all without hardware.

Run with:
  py tools/test_packets.py
  py tools/test_packets.py -v      # verbose output

Exit code 0 = all tests passed.
"""

import struct
import sys
import traceback
from typing import List

# ─────────────────────────────────────────────────────────────────────────────
# Mirror of firmware constants (keep in sync with experience_packet.h)
# ─────────────────────────────────────────────────────────────────────────────

PACKET_MAGIC    = 0x4E4D5650   # "NMVP"
PACKET_VERSION  = 1
SESSION_MAGIC   = 0x4E4D4C47   # "NMLG"
JOINT_COUNT     = 6
VISION_BYTES    = 128
HEADER_SIZE     = 32

# ExperiencePacket packed layout (little-endian):
#  magic(4) version(1) source_node(1) payload_len(2)
#  timestamp_us(4) seq(4) joints(6×f4) vision(128×B) crc16(2)
PACKET_FMT  = "<IBBHII6f128BH"
PACKET_SIZE = struct.calcsize(PACKET_FMT)

# SessionHeader packed layout:
#  magic(4) session_id(4) start_epoch_s(4) packet_count(4) reserved(16)
HEADER_FMT  = "<IIII16s"


# ─────────────────────────────────────────────────────────────────────────────
# CRC-CCITT — exact mirror of firmware crc16_ccitt()
# ─────────────────────────────────────────────────────────────────────────────

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


# ─────────────────────────────────────────────────────────────────────────────
# Packet helpers — mirror finalize_packet() / validate_packet()
# ─────────────────────────────────────────────────────────────────────────────

NodeId = {
    "node0": 0,
    "node1": 1,
    "node2": 2,
    "node3": 3,
}


def build_packet(
    source_node: int,
    timestamp_us: int,
    seq: int,
    joints: List[float],
    vision: bytes,
) -> bytes:
    """Construct and finalize an ExperiencePacket (mirrors finalize_packet)."""
    assert len(joints) == JOINT_COUNT
    assert len(vision) == VISION_BYTES

    # Pack with crc16=0 first
    raw = struct.pack(
        PACKET_FMT,
        PACKET_MAGIC,
        PACKET_VERSION,
        source_node,
        PACKET_SIZE,       # payload_len
        timestamp_us,
        seq,
        *joints,
        *vision,
        0,                 # crc16 placeholder
    )
    crc = crc16_ccitt(raw)
    # Overwrite last 2 bytes with computed CRC
    return raw[:-2] + struct.pack("<H", crc)


def validate_packet(raw: bytes) -> bool:
    """Mirror validate_packet(): check magic, version, CRC."""
    if len(raw) != PACKET_SIZE:
        return False
    fields = struct.unpack(PACKET_FMT, raw)
    magic, version = fields[0], fields[1]
    if magic != PACKET_MAGIC or version != PACKET_VERSION:
        return False
    stored_crc = fields[-1]
    zeroed = raw[:-2] + b"\x00\x00"
    return crc16_ccitt(zeroed) == stored_crc


def build_session_header(session_id: int, start_epoch_s: int, packet_count: int) -> bytes:
    return struct.pack(HEADER_FMT, SESSION_MAGIC, session_id, start_epoch_s, packet_count, b"\x00" * 16)


def parse_session_header(data: bytes):
    return struct.unpack_from(HEADER_FMT, data, 0)


# ─────────────────────────────────────────────────────────────────────────────
# Minimal test runner
# ─────────────────────────────────────────────────────────────────────────────

_results: List[tuple] = []
_verbose = "-v" in sys.argv


def test(name: str):
    """Decorator that registers and runs a test function."""
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
# ── Struct layout ──────────────────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

@test("packet size is 170 bytes")
def _():
    assert PACKET_SIZE == 170, f"Expected 170, got {PACKET_SIZE}"


@test("session header size is 32 bytes")
def _():
    assert struct.calcsize(HEADER_FMT) == HEADER_SIZE, \
        f"Expected 32, got {struct.calcsize(HEADER_FMT)}"


# ─────────────────────────────────────────────────────────────────────────────
# ── CRC codec ──────────────────────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

@test("crc16_ccitt known-good vector: empty string → 0xFFFF")
def _():
    # CRC of empty data is the init value
    assert crc16_ccitt(b"") == 0xFFFF

@test("crc16_ccitt known-good vector: 0x31..0x39 → 0x29B1")
def _():
    # Standard CRC-CCITT test vector for '123456789'
    result = crc16_ccitt(b"123456789")
    assert result == 0x29B1, f"Expected 0x29B1, got 0x{result:04X}"

@test("crc16_ccitt is sensitive to byte order")
def _():
    assert crc16_ccitt(b"\x01\x02") != crc16_ccitt(b"\x02\x01")

@test("crc16_ccitt detects single-bit flip")
def _():
    data = b"\xAB\xCD\xEF\x01"
    original = crc16_ccitt(data)
    flipped = bytearray(data)
    flipped[1] ^= 0x01
    assert crc16_ccitt(bytes(flipped)) != original


# ─────────────────────────────────────────────────────────────────────────────
# ── finalize / validate round-trip ─────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

def _make_node1_packet(seq=1, ts=1000):
    joints = [45.0, 90.0, 120.0, 30.0, 60.0, 0.0]
    vision = bytes(VISION_BYTES)  # zeros (node1 sends no vision)
    return build_packet(NodeId["node1"], ts, seq, joints, vision)

def _make_cam_packet(source_node, seq=1, ts=2000, brightness_pattern=None):
    joints = [0.0] * JOINT_COUNT
    if brightness_pattern is None:
        # Simulate 8×8 grid: first 64 bytes vary, rest zero
        vision = bytes([i * 4 % 256 for i in range(64)] + [0] * 64)
    else:
        vision = brightness_pattern
    return build_packet(source_node, ts, seq, joints, vision)


@test("node1 packet validates correctly")
def _():
    raw = _make_node1_packet()
    assert validate_packet(raw), "node1 packet failed validation"

@test("node2 cam packet validates correctly")
def _():
    raw = _make_cam_packet(NodeId["node2"])
    assert validate_packet(raw), "node2 packet failed validation"

@test("node3 cam packet validates correctly")
def _():
    raw = _make_cam_packet(NodeId["node3"])
    assert validate_packet(raw), "node3 packet failed validation"

@test("packet with corrupted magic fails validation")
def _():
    raw = bytearray(_make_node1_packet())
    raw[0] ^= 0xFF   # corrupt magic byte 0
    assert not validate_packet(bytes(raw))

@test("packet with corrupted version fails validation")
def _():
    raw = bytearray(_make_node1_packet())
    raw[4] = 0x99    # version is at byte offset 4
    assert not validate_packet(bytes(raw))

@test("packet with single-byte data corruption fails CRC")
def _():
    raw = bytearray(_make_node1_packet())
    raw[10] ^= 0x01   # flip a bit in timestamp_us
    assert not validate_packet(bytes(raw))

@test("packet with corrupted CRC field fails validation")
def _():
    raw = bytearray(_make_node1_packet())
    raw[-1] ^= 0xFF   # flip high byte of crc16
    assert not validate_packet(bytes(raw))

@test("payload_len field equals PACKET_SIZE")
def _():
    raw = _make_node1_packet()
    fields = struct.unpack(PACKET_FMT, raw)
    payload_len = fields[3]
    assert payload_len == PACKET_SIZE, f"Expected {PACKET_SIZE}, got {payload_len}"

@test("source_node field is preserved through finalize/validate")
def _():
    raw = _make_cam_packet(NodeId["node2"])
    fields = struct.unpack(PACKET_FMT, raw)
    assert fields[2] == NodeId["node2"]

@test("joint values are preserved through finalize/validate")
def _():
    joints_in = [10.5, 20.25, 30.125, 45.0, 90.0, 179.999]
    raw = build_packet(NodeId["node1"], 5000, 1, joints_in, bytes(VISION_BYTES))
    fields = struct.unpack(PACKET_FMT, raw)
    joints_out = list(fields[6:12])
    for i, (a, b) in enumerate(zip(joints_in, joints_out)):
        assert abs(a - b) < 1e-4, f"joint[{i}]: expected {a}, got {b}"

@test("vision features are preserved through finalize/validate")
def _():
    vision_in = bytes(range(64)) + bytes(64)  # 0-63 then zeros
    raw = build_packet(NodeId["node2"], 1000, 1, [0.0]*6, vision_in)
    fields = struct.unpack(PACKET_FMT, raw)
    vision_out = bytes(fields[12:140])
    assert vision_out == vision_in

@test("seq field is preserved through finalize/validate")
def _():
    for seq_val in (0, 1, 255, 65535, 0xFFFFFFFF):
        raw = build_packet(NodeId["node1"], 0, seq_val, [0.0]*6, bytes(VISION_BYTES))
        fields = struct.unpack(PACKET_FMT, raw)
        assert fields[5] == seq_val, f"seq {seq_val} not preserved"

@test("truncated packet fails validation")
def _():
    raw = _make_node1_packet()[:-1]   # one byte short
    assert not validate_packet(raw)

@test("oversized packet fails validation")
def _():
    raw = _make_node1_packet() + b"\x00"
    assert not validate_packet(raw)


# ─────────────────────────────────────────────────────────────────────────────
# ── Sequence continuity (mirrors validate_log.py gap detection) ────────────
# ─────────────────────────────────────────────────────────────────────────────

def detect_gaps(packets_raw: List[bytes]) -> dict:
    """Replicate the per-source seq gap detection from validate_log.py."""
    seq_last = {}
    gaps = {}
    for raw in packets_raw:
        if len(raw) != PACKET_SIZE:
            continue
        fields = struct.unpack(PACKET_FMT, raw)
        source = fields[2]
        seq    = fields[5]
        if source in seq_last:
            expected = seq_last[source] + 1
            if seq != expected:
                gaps.setdefault(source, []).append((expected, seq))
        seq_last[source] = seq
    return gaps


@test("continuous sequence produces no gaps")
def _():
    pkts = [_make_node1_packet(seq=i, ts=i*1000) for i in range(1, 21)]
    gaps = detect_gaps(pkts)
    assert not gaps, f"Unexpected gaps: {gaps}"

@test("single dropped packet is detected as one gap")
def _():
    seqs = list(range(1, 11))
    seqs.remove(5)   # drop packet 5
    pkts = [_make_node1_packet(seq=s, ts=s*1000) for s in seqs]
    gaps = detect_gaps(pkts)
    assert NodeId["node1"] in gaps
    assert len(gaps[NodeId["node1"]]) == 1
    assert gaps[NodeId["node1"]][0] == (5, 6)

@test("multiple dropped packets each register as a gap event")
def _():
    seqs = [1, 2, 3, 7, 8, 15]   # gaps at 4 and 9
    pkts = [_make_node1_packet(seq=s, ts=s*1000) for s in seqs]
    gaps = detect_gaps(pkts)
    assert len(gaps[NodeId["node1"]]) == 2

@test("seq wraparound from 0xFFFFFFFF to 0 is detected as a gap")
def _():
    # uint32 max then 0 — the firmware doesn't wrap, but we should detect it
    pkts = [
        _make_node1_packet(seq=0xFFFFFFFE, ts=1000),
        _make_node1_packet(seq=0xFFFFFFFF, ts=2000),
        _make_node1_packet(seq=0,          ts=3000),   # seq went backward
    ]
    gaps = detect_gaps(pkts)
    assert NodeId["node1"] in gaps

@test("independent source nodes have independent seq tracking")
def _():
    # node2 and node3 both start at seq=1 — that should NOT trigger gaps
    pkts = []
    for i in range(1, 6):
        pkts.append(_make_cam_packet(NodeId["node2"], seq=i, ts=i*1000))
        pkts.append(_make_cam_packet(NodeId["node3"], seq=i, ts=i*1000))
    gaps = detect_gaps(pkts)
    assert not gaps, f"False gaps across nodes: {gaps}"

@test("interleaved node1 and node2 streams both continuous produce no gaps")
def _():
    pkts = []
    for i in range(1, 11):
        pkts.append(_make_node1_packet(seq=i, ts=i*1000))
        pkts.append(_make_cam_packet(NodeId["node2"], seq=i, ts=i*1000 + 1))
    gaps = detect_gaps(pkts)
    assert not gaps


# ─────────────────────────────────────────────────────────────────────────────
# ── SessionHeader ───────────────────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

@test("session header magic round-trips correctly")
def _():
    raw = build_session_header(session_id=7, start_epoch_s=3600, packet_count=0)
    magic, sid, epoch, count, _ = parse_session_header(raw)
    assert magic == SESSION_MAGIC
    assert sid == 7
    assert epoch == 3600
    assert count == 0

@test("session header packet_count field can be updated in-place")
def _():
    raw = bytearray(build_session_header(1, 0, 0))
    # packet_count is at byte offset 12 (3rd uint32)
    struct.pack_into("<I", raw, 12, 42)
    _, _, _, count, _ = parse_session_header(bytes(raw))
    assert count == 42

@test("log file layout: first packet starts at offset 32")
def _():
    hdr = build_session_header(1, 0, 1)
    pkt = _make_node1_packet(seq=1)
    log = hdr + pkt
    assert len(log) == HEADER_SIZE + PACKET_SIZE
    # Read packet back from offset 32
    raw_back = log[HEADER_SIZE:]
    assert validate_packet(raw_back)

@test("multi-packet log file: all packets parse and validate")
def _():
    hdr = build_session_header(1, 0, 5)
    log = hdr
    for i in range(1, 6):
        log += _make_node1_packet(seq=i, ts=i * 2500)
    assert len(log) == HEADER_SIZE + 5 * PACKET_SIZE
    for idx in range(5):
        offset = HEADER_SIZE + idx * PACKET_SIZE
        assert validate_packet(log[offset: offset + PACKET_SIZE]), \
            f"Packet {idx} failed validation in multi-packet log"


# ─────────────────────────────────────────────────────────────────────────────
# ── Cam node feature vector constraints ────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

@test("cam packet 8x8 grid: indices 0-63 carry data, 64-127 are reserved zero")
def _():
    grid = bytes([i * 3 % 256 for i in range(64)])
    vision = grid + bytes(64)   # reserved half is zero
    raw = build_packet(NodeId["node2"], 1000, 1, [0.0]*6, vision)
    fields = struct.unpack(PACKET_FMT, raw)
    vision_out = bytes(fields[12:140])
    assert vision_out[:64] == grid,          "Grid data corrupted"
    assert vision_out[64:] == bytes(64),     "Reserved bytes should be zero"

@test("cam packet brightness values stay in uint8 range 0-255")
def _():
    for brightness in (0, 127, 255):
        vision = bytes([brightness] * 64 + [0] * 64)
        raw = build_packet(NodeId["node3"], 0, 1, [0.0]*6, vision)
        fields = struct.unpack(PACKET_FMT, raw)
        for v in fields[12:76]:
            assert 0 <= v <= 255

@test("all-zero vision grid is valid (dark scene)")
def _():
    vision = bytes(VISION_BYTES)
    raw = build_packet(NodeId["node2"], 0, 1, [0.0]*6, vision)
    assert validate_packet(raw)

@test("all-255 vision grid is valid (saturated scene)")
def _():
    vision = bytes([255] * 64 + [0] * 64)
    raw = build_packet(NodeId["node2"], 0, 1, [0.0]*6, vision)
    assert validate_packet(raw)


# ─────────────────────────────────────────────────────────────────────────────
# ── Node1 joint value constraints ──────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

@test("node1 joints: ADC full-scale 0.0 → 1.0 range survives round-trip")
def _():
    for v in (0.0, 0.5, 1.0):
        joints = [v] * JOINT_COUNT
        raw = build_packet(NodeId["node1"], 0, 1, joints, bytes(VISION_BYTES))
        fields = struct.unpack(PACKET_FMT, raw)
        for j in fields[6:12]:
            assert abs(j - v) < 1e-6

@test("node1 joints: negative values are preserved (signed float)")
def _():
    joints = [-1.0, -0.5, 0.0, 0.5, 1.0, -90.0]
    raw = build_packet(NodeId["node1"], 0, 1, joints, bytes(VISION_BYTES))
    fields = struct.unpack(PACKET_FMT, raw)
    for i, (a, b) in enumerate(zip(joints, fields[6:12])):
        assert abs(a - b) < 1e-4, f"joint[{i}] mismatch"


# ─────────────────────────────────────────────────────────────────────────────
# ── Results summary ─────────────────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────

def main():
    passed = [r for r in _results if r[1]]
    failed = [r for r in _results if not r[1]]

    print(f"\nNodeMesh IL Packet Tests — {len(passed)}/{len(_results)} passed")
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
