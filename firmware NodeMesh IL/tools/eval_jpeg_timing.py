"""eval_jpeg_timing.py — Static timing analysis for ESP32-CAM feature extraction.

Compares the current raw-grayscale path against a hypothetical JPEG-decode path
for the 8×8 mean-brightness grid used in ExperiencePacket.vision_features.

References
----------
OV2640 datasheet (OMNIVISION, DS-OV2640-1.8) — QQVGA pixel clock budget
TJpgDec (Elm-ChaN) benchmark figures — ~5–10 ms at 240 MHz for QQVGA JPEG
ESP32 Technical Reference Manual — DVP camera DMA throughput
"""

import math
from dataclasses import dataclass, field
from typing import Optional

# ---------------------------------------------------------------------------
# Hardware constants
# ---------------------------------------------------------------------------

CPU_MHZ          = 240        # ESP32 (AI Thinker) CPU frequency
XCLK_MHZ         = 10         # XCLK fed to OV2640 (cfg.xclk_freq_hz = 10_000_000)
FRAME_W          = 160        # QQVGA width  (pixels)
FRAME_H          = 120        # QQVGA height (pixels)
FRAME_PIXELS     = FRAME_W * FRAME_H                  # 19 200
RAW_BYTES        = FRAME_PIXELS                       # 1 byte/px grayscale
JPEG_BYTES_MIN   = 3_000      # typical QQVGA JPEG @ quality 12 (low)
JPEG_BYTES_MAX   = 8_000      # typical QQVGA JPEG @ quality 12 (high texture)

# ---------------------------------------------------------------------------
# Timing models (all values in milliseconds)
# ---------------------------------------------------------------------------

@dataclass
class PathTiming:
    name: str
    capture_ms: float         # time to acquire one frame buffer from camera DMA
    decode_ms: float          # time to decode / iterate pixels
    feature_ms: float         # time to compute 8×8 grid from raw pixel data
    send_ms: float            # ESP-NOW packet TX (170-byte packet @ ~1 Mbps PHY)
    notes: list = field(default_factory=list)

    @property
    def total_ms(self) -> float:
        return self.capture_ms + self.decode_ms + self.feature_ms + self.send_ms

    @property
    def max_fps(self) -> float:
        return 1_000.0 / self.total_ms if self.total_ms > 0 else float("inf")


def model_raw_path() -> PathTiming:
    """
    Current implementation:
      - PIXFORMAT_GRAYSCALE, FRAMESIZE_QQVGA
      - OV2640 outputs raw Y (19 200 B) via DVP DMA
      - extractFeatures() iterates 19 200 bytes: single nested loop
      - delay(33) pads to 30 FPS in firmware
    """
    # OV2640 QQVGA @ 10 MHz XCLK: sensor internal clock = XCLK/2 = 5 MHz.
    # Row time ≈ (W + H-blanking) / pclk.  At QQVGA pclk ~5 MHz, 1 row ≈ 80 µs.
    # 120 rows → ~9.6 ms sensor readout.  Add DMA transfer of 19 200 B @ AHB
    # 40 MB/s → 0.48 ms.  Round up with driver overhead.
    capture_ms = 10.0

    # extractFeatures(): ~19 200 loop iterations on 240 MHz CPU.
    # Throughput ≈ 1 iteration / 4 cycles → 19 200 / (240e6/4) ≈ 0.32 ms.
    feature_ms = 0.35

    # ESP-NOW transmit (170-byte ExperiencePacket): ~1 Mbps effective PHY.
    # 170 B × 8 bits / 1e6 = 1.36 ms.  With WiFi stack overhead ≈ 2–3 ms.
    send_ms = 3.0

    return PathTiming(
        name="Raw grayscale (current)",
        capture_ms=capture_ms,
        decode_ms=0.0,        # no decode step
        feature_ms=feature_ms,
        send_ms=send_ms,
        notes=[
            "DMA transfers raw 19 200 B — no compression/decompression.",
            "delay(33) in firmware adds up to 33 ms padding to target 30 FPS.",
            "Feature extraction is O(W×H) trivial loop — not a bottleneck.",
        ],
    )


def model_jpeg_path(jpeg_bytes: int = 5_500) -> PathTiming:
    """
    Hypothetical JPEG path:
      - PIXFORMAT_JPEG, FRAMESIZE_QQVGA (OV2640 hardware compresses on-sensor)
      - Software JPEG decode via TJpgDec (Elm-ChaN, single-file MIT library)
      - Luminance-only decode to RAM → compute 8×8 grid

    Why someone might consider it
    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Smaller JPEG payload → faster DMA transfer. But the sensor exposure and
    row-readout time dominates — you cannot skip those clock cycles regardless
    of output format.  The decode step then adds software latency with no
    corresponding capture speedup.
    """
    # Capture: sensor exposure + readout is format-independent.
    # OV2640 internal JPEG compressor runs in parallel with DVP output — the
    # frame is still exposed for the same duration.  DMA transfer is faster
    # (jpeg_bytes vs 19 200 B) but negligible vs exposure time.
    capture_ms = 10.0  # same sensor exposure; smaller DMA saves ~0.3 ms

    # TJpgDec benchmark at 240 MHz (empirically measured by library author):
    #   QVGA (320×240) → ~10–15 ms
    #   QQVGA (160×120) → ~4–8 ms (proportional to pixel count)
    # With luminance-only output (no RGB conversion): ~3–6 ms.
    # Pessimistic: 8 ms; Optimistic: 3 ms.  Use midpoint.
    decode_ms = 5.5

    # Feature extraction from decoded Y buffer: same 0.35 ms.
    feature_ms = 0.35

    # Same ESP-NOW overhead.
    send_ms = 3.0

    return PathTiming(
        name=f"JPEG decode (hypothetical, {jpeg_bytes} B JPEG)",
        capture_ms=capture_ms,
        decode_ms=decode_ms,
        feature_ms=feature_ms,
        send_ms=send_ms,
        notes=[
            f"OV2640 outputs hardware-compressed JPEG (~{jpeg_bytes} B).",
            "TJpgDec (Elm-ChaN) provides luminance-only decode mode.",
            "No hardware JPEG decoder on ESP32 — pure software at 240 MHz.",
            "decode_ms estimate from TJpgDec author benchmarks (QQVGA, 240 MHz).",
            "Sensor exposure time is format-independent — capture_ms unchanged.",
        ],
    )


# ---------------------------------------------------------------------------
# Partial-JPEG optimisation: extract only 8×8 DC coefficients
# ---------------------------------------------------------------------------

def model_dc_only_path() -> PathTiming:
    """
    Aggressive optimisation: parse JPEG bitstream, extract only the DC
    coefficient of each 8×8 luminance block — skip AC coefficients and all
    colour blocks entirely.

    QQVGA at JPEG quality 12:
      - 160×120 / 8×8 = 300 luminance MCUs
      - Average DC VLC symbol ≈ 4 bits → 1 200 bits total DC data
      - With Huffman parsing overhead: ~50 µs at 240 MHz (estimate)

    This matches exactly the 8×8 grid mean-brightness semantic: each DC
    coefficient is proportional to the mean luma of that block.  (DC = Σpix / N,
    scaled by cosine basis = mean × 8.)
    """
    capture_ms = 10.0

    # Parsing JPEG header + Huffman table + 300 DC symbols at 240 MHz.
    # This is a custom partial decoder — no existing library; estimated.
    decode_ms = 0.15  # optimistic but plausible

    feature_ms = 0.05  # trivial: DC coefficients are already per-block means

    send_ms = 3.0

    return PathTiming(
        name="DC-only partial JPEG (hypothetical, custom parser)",
        capture_ms=capture_ms,
        decode_ms=decode_ms,
        feature_ms=feature_ms,
        send_ms=send_ms,
        notes=[
            "Custom parser: read JPEG SOF/DHT/SOS, decode only DC VLC per MCU.",
            "QQVGA has 300 luma MCUs → 300 DC values = exactly 64 needed blocks",
            "  + 236 unused blocks (outside 8×8 grid — can abort early).",
            "No existing library implements this for ESP32; ~200 lines of C.",
            "Fragile against OV2640 restart artefacts (RST markers in stream).",
        ],
    )


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

def report(path: PathTiming, target_fps: float = 30.0) -> None:
    margin_ms = (1_000.0 / target_fps) - path.total_ms
    feasible = path.total_ms <= (1_000.0 / target_fps)

    print(f"\n{'='*62}")
    print(f"  {path.name}")
    print(f"{'='*62}")
    print(f"  Capture time       : {path.capture_ms:6.2f} ms")
    if path.decode_ms > 0:
        print(f"  Decode time        : {path.decode_ms:6.2f} ms")
    print(f"  Feature extraction : {path.feature_ms:6.2f} ms")
    print(f"  ESP-NOW send       : {path.send_ms:6.2f} ms")
    print(f"  ─────────────────────────────")
    print(f"  Total              : {path.total_ms:6.2f} ms  ({path.max_fps:.1f} FPS max)")
    print(f"  Margin @ {target_fps:.0f} FPS     : {margin_ms:+.2f} ms  "
          f"({'OK — firmware delay() absorbs slack' if feasible else 'TIGHT — over budget'})")
    print()
    for note in path.notes:
        print(f"  • {note}")


def recommendation(paths: list[PathTiming]) -> str:
    lines = [
        "",
        "RECOMMENDATION",
        "──────────────",
        "The raw grayscale path already completes in ~13.4 ms/frame,",
        "leaving ~19.6 ms of slack before the 33 ms frame budget.",
        "The firmware delay(33) absorbs this — 30 FPS is comfortably achievable.",
        "",
        "Switching to full JPEG decode (TJpgDec) adds ~5.5 ms decode latency",
        "with no corresponding capture speedup (sensor exposure is format-",
        "independent).  Net effect: 5.5 ms slower per frame for zero benefit.",
        "",
        "The DC-only partial decoder could in principle match or beat the raw",
        "path, but requires ~200 lines of custom JPEG bitstream parsing code,",
        "is fragile against OV2640 restart markers, and gains nothing over the",
        "current approach since 30 FPS is already met with headroom.",
        "",
        "CONCLUSION: Keep PIXFORMAT_GRAYSCALE.  No JPEG decode path needed.",
        "            The 30 FPS target is met with ~19.6 ms / frame to spare.",
        "            Revisit only if the feature vector grows > 128 bytes or",
        "            the grid resolution increases to > 16×16.",
    ]
    return "\n".join(lines)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Static timing analysis: raw-grayscale vs JPEG-decode on ESP32-CAM"
    )
    parser.add_argument("--jpeg-bytes", type=int, default=5_500,
                        help="Estimated JPEG frame size in bytes (default: 5500)")
    parser.add_argument("--target-fps", type=float, default=30.0,
                        help="Target frame rate (default: 30)")
    args = parser.parse_args()

    raw  = model_raw_path()
    jpeg = model_jpeg_path(jpeg_bytes=args.jpeg_bytes)
    dc   = model_dc_only_path()

    print()
    print("NodeMesh IL — ESP32-CAM Feature Extraction Timing Analysis")
    print(f"Hardware : ESP32 (AI Thinker OV2640), CPU={CPU_MHZ} MHz, XCLK={XCLK_MHZ} MHz")
    print(f"Frame    : {FRAME_W}×{FRAME_H} QQVGA,  {FRAME_PIXELS} px,  raw={RAW_BYTES} B,  "
          f"JPEG≈{JPEG_BYTES_MIN}–{JPEG_BYTES_MAX} B")

    for path in [raw, jpeg, dc]:
        report(path, target_fps=args.target_fps)

    print()
    print(recommendation([raw, jpeg, dc]))
    print()
