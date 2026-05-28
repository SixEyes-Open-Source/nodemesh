#!/usr/bin/env python3
"""
validate_log.py — Offline validator for NodeMesh IL SD log files.

Binary format on SD card (little-endian, packed):
  Offset 0: SessionHeader (32 bytes)
  Offset 32+: ExperiencePacket records (170 bytes each), circular

Usage:
  py validate_log.py node0_log.bin
  py validate_log.py node0_log.bin --dump-packets 10
  py validate_log.py node0_log.bin --csv out.csv
"""

import argparse
import csv
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Constants mirrored from firmware headers
# ---------------------------------------------------------------------------

SESSION_MAGIC   = 0x4E4D4C47   # "NMLG"
PACKET_MAGIC    = 0x4E4D5650   # "NMVP"
PACKET_VERSION  = 1
HEADER_SIZE     = 32           # sizeof(SessionHeader)
JOINT_COUNT     = 6
VISION_BYTES    = 128

# struct ExperiencePacket (packed, little-endian):
#  magic(4) version(1) source_node(1) payload_len(2)
#  timestamp_us(4) seq(4) joints(6×f4) vision(128×u1) crc16(2)
PACKET_FMT  = "<IBBHII6f128Bh"   # NOTE: crc16 stored as signed short on wire
PACKET_SIZE = struct.calcsize(PACKET_FMT)   # should be 170

assert PACKET_SIZE == 170, f"Unexpected packet size {PACKET_SIZE}"

# struct SessionHeader (packed, little-endian):
#  magic(4) session_id(4) start_epoch_s(4) packet_count(4) reserved(16)
HEADER_FMT  = "<IIII16s"
assert struct.calcsize(HEADER_FMT) == HEADER_SIZE


# ---------------------------------------------------------------------------
# CRC-CCITT (matches firmware crc16_ccitt)
# ---------------------------------------------------------------------------

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def verify_crc(raw_packet: bytes) -> bool:
    """Zero out the last 2 bytes (crc16 field), recompute, compare."""
    expected = struct.unpack_from("<H", raw_packet, PACKET_SIZE - 2)[0]
    zeroed = raw_packet[:-2] + b"\x00\x00"
    return crc16_ccitt(zeroed) == expected


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class SessionInfo:
    magic: int
    session_id: int
    start_epoch_s: int
    packet_count_in_header: int
    reserved: bytes


@dataclass
class PacketRecord:
    offset: int
    index: int          # 0-based record index in file
    source_node: int
    timestamp_us: int
    seq: int
    joints: tuple
    vision: bytes
    crc_ok: bool


@dataclass
class ValidationResult:
    session: Optional[SessionInfo] = None
    total_packets: int = 0
    crc_errors: int = 0
    seq_gaps: dict = field(default_factory=dict)   # source_node → list of (expected, got) tuples
    seq_last: dict = field(default_factory=dict)   # source_node → last seen seq
    truncated_tail_bytes: int = 0
    packets: list = field(default_factory=list)    # filled only when --dump-packets requested


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def parse_session_header(data: bytes) -> SessionInfo:
    if len(data) < HEADER_SIZE:
        raise ValueError(f"File too small for header ({len(data)} bytes)")
    magic, sid, epoch, count, reserved = struct.unpack_from(HEADER_FMT, data, 0)
    return SessionInfo(magic, sid, epoch, count, reserved)


def parse_log(path: Path, max_dump: int = 0) -> ValidationResult:
    result = ValidationResult()

    raw = path.read_bytes()
    if len(raw) < HEADER_SIZE:
        print(f"ERROR: file is only {len(raw)} bytes — no header.", file=sys.stderr)
        sys.exit(1)

    # --- Header ---
    hdr = parse_session_header(raw)
    result.session = hdr

    if hdr.magic != SESSION_MAGIC:
        print(
            f"WARNING: session header magic mismatch: "
            f"got 0x{hdr.magic:08X}, expected 0x{SESSION_MAGIC:08X}",
            file=sys.stderr,
        )

    # --- Packets ---
    offset = HEADER_SIZE
    idx = 0

    while offset + PACKET_SIZE <= len(raw):
        chunk = raw[offset: offset + PACKET_SIZE]
        fields = struct.unpack(PACKET_FMT, chunk)

        (magic, version, source_node, payload_len,
         timestamp_us, seq, *rest) = fields

        joints = tuple(rest[:JOINT_COUNT])
        vision = bytes(rest[JOINT_COUNT: JOINT_COUNT + VISION_BYTES])
        # crc16 is last field (rest[-1]); stored as signed int from struct fmt
        # but we verify via raw bytes
        crc_ok = verify_crc(chunk)

        if magic != PACKET_MAGIC:
            # Likely hit padding/garbage in circular region; stop.
            print(
                f"  [offset 0x{offset:08X}] packet #{idx}: bad magic "
                f"0x{magic:08X} — stopping parse.",
                file=sys.stderr,
            )
            break

        if not crc_ok:
            result.crc_errors += 1

        # Sequence gap detection (per source_node)
        if source_node in result.seq_last:
            expected_seq = result.seq_last[source_node] + 1
            if seq != expected_seq:
                gaps = result.seq_gaps.setdefault(source_node, [])
                gaps.append((expected_seq, seq))
        result.seq_last[source_node] = seq

        rec = PacketRecord(
            offset=offset,
            index=idx,
            source_node=source_node,
            timestamp_us=timestamp_us,
            seq=seq,
            joints=joints,
            vision=vision,
            crc_ok=crc_ok,
        )

        if max_dump > 0 and idx < max_dump:
            result.packets.append(rec)

        result.total_packets += 1
        offset += PACKET_SIZE
        idx += 1

    # Leftover bytes after last full packet
    tail = len(raw) - offset
    if tail > 0:
        result.truncated_tail_bytes = tail

    return result


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

NODE_NAMES = {
    0: "node0-orch",
    1: "node1-input",
    2: "node2-cam-global",
    3: "node3-cam-wrist",
}


def print_report(result: ValidationResult, path: Path) -> None:
    hdr = result.session
    print(f"\n=== NodeMesh IL Log Validator ===")
    print(f"File          : {path}")
    print(f"File size     : {path.stat().st_size:,} bytes")
    print()
    print(f"--- Session Header ---")
    if hdr:
        magic_ok = "OK" if hdr.magic == SESSION_MAGIC else f"BAD (0x{hdr.magic:08X})"
        print(f"  Magic       : {magic_ok}")
        print(f"  Session ID  : {hdr.session_id}")
        print(f"  Start epoch : {hdr.start_epoch_s} s (since boot)")
        print(f"  Pkt count   : {hdr.packet_count_in_header} (as recorded in header)")
    print()
    print(f"--- Packet Scan ---")
    print(f"  Parsed       : {result.total_packets}")
    print(f"  CRC errors   : {result.crc_errors}")
    if result.truncated_tail_bytes:
        print(f"  Truncated    : {result.truncated_tail_bytes} trailing bytes (incomplete packet)")

    # Header count vs actual
    if hdr and hdr.packet_count_in_header != result.total_packets:
        print(
            f"  WARNING: header count ({hdr.packet_count_in_header}) != "
            f"parsed count ({result.total_packets})"
        )

    print()
    print(f"--- Sequence Gaps ---")
    if not result.seq_gaps:
        print("  None detected.")
    else:
        total_gaps = sum(len(v) for v in result.seq_gaps.values())
        print(f"  Total gap events: {total_gaps}")
        for node, gaps in sorted(result.seq_gaps.items()):
            name = NODE_NAMES.get(node, f"node{node}")
            print(f"  {name} ({len(gaps)} gaps):")
            for exp, got in gaps[:20]:
                dropped = got - exp
                print(f"    expected seq {exp}, got {got}  (dropped {dropped})")
            if len(gaps) > 20:
                print(f"    ... and {len(gaps) - 20} more")

    print()
    print(f"--- Per-Source Summary ---")
    if not result.seq_last:
        print("  No packets found.")
    else:
        for node in sorted(result.seq_last):
            name = NODE_NAMES.get(node, f"node{node}")
            last = result.seq_last[node]
            gaps = len(result.seq_gaps.get(node, []))
            print(f"  {name:20s}  last_seq={last:<8}  gap_events={gaps}")

    # Overall pass/fail
    print()
    ok = result.crc_errors == 0 and not result.seq_gaps
    print(f"=== {'PASS' if ok else 'FAIL'} ===\n")


def dump_packets(packets: list, max_dump: int) -> None:
    if not packets:
        return
    print(f"--- First {len(packets)} Packets ---")
    for rec in packets:
        name = NODE_NAMES.get(rec.source_node, f"node{rec.source_node}")
        joints_str = ", ".join(f"{j:7.2f}" for j in rec.joints)
        crc_tag = "" if rec.crc_ok else " [CRC-ERR]"
        print(
            f"  #{rec.index:5d}  @0x{rec.offset:08X}  {name:<20s}"
            f"  seq={rec.seq:<8}  ts={rec.timestamp_us}us"
            f"  joints=[{joints_str}]{crc_tag}"
        )


def write_csv(result: ValidationResult, csv_path: Path) -> None:
    vision_headers = [f"vision_{i}" for i in range(VISION_BYTES)]
    fieldnames = (
        ["index", "offset", "source_node", "timestamp_us", "seq", "crc_ok"]
        + [f"joint_{i}" for i in range(JOINT_COUNT)]
        + vision_headers
    )
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        # Re-parse; we only have full records if max_dump covered everything.
        # For CSV we re-read from the result's packet list.
        for rec in result.packets:
            row = {
                "index": rec.index,
                "offset": rec.offset,
                "source_node": rec.source_node,
                "timestamp_us": rec.timestamp_us,
                "seq": rec.seq,
                "crc_ok": int(rec.crc_ok),
            }
            for i, j in enumerate(rec.joints):
                row[f"joint_{i}"] = f"{j:.6f}"
            for i, v in enumerate(rec.vision):
                row[f"vision_{i}"] = v
            writer.writerow(row)
    print(f"CSV written: {csv_path} ({len(result.packets)} rows)")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Validate a NodeMesh IL binary log file from SD card."
    )
    parser.add_argument("log_file", help="Path to node0_log.bin copied from SD card")
    parser.add_argument(
        "--dump-packets", metavar="N", type=int, default=0,
        help="Print the first N packet records to stdout",
    )
    parser.add_argument(
        "--csv", metavar="FILE",
        help="Export all packets to a CSV file (requires --dump-packets 0 with full read)",
    )
    args = parser.parse_args()

    path = Path(args.log_file)
    if not path.exists():
        print(f"ERROR: file not found: {path}", file=sys.stderr)
        sys.exit(1)

    # For CSV we need all packets in memory; set max_dump to large number
    max_dump = args.dump_packets
    if args.csv:
        max_dump = 10_000_000  # effectively unlimited

    result = parse_log(path, max_dump=max_dump)
    print_report(result, path)

    if args.dump_packets > 0 and not args.csv:
        dump_packets(result.packets[:args.dump_packets], args.dump_packets)

    if args.csv:
        write_csv(result, Path(args.csv))

    sys.exit(0 if result.crc_errors == 0 and not result.seq_gaps else 1)


if __name__ == "__main__":
    main()
