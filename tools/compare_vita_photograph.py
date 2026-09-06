#!/usr/bin/env python3
"""Compare a captured DK64 photograph with its actual source framebuffer.

Inputs are N64 byte order: a 320x240 RGBA16 source readback and the original
0xA000-byte photograph allocation (ten 32x64 tiles, central 160x128 crop).
No ROM, saves or capture data are included in this utility.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def sepia(value):
    # Original 806FFC04: integer channels, the 93/45 curves, truncation, and
    # the input alpha bit. Use integer arithmetic as an independent oracle
    # for the generated function's floating-point operations and rounding mode.
    total = (value >> 11) + ((value >> 6) & 31) + ((value >> 1) & 31)
    red = 3 + 28 * total // 93
    green = 3 + 19 * total // 93
    blue = 0 if total < 48 else 18 * (total - 48) // 45
    return (red << 11) | (green << 6) | (blue << 1) | (value & 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("framebuffer", type=Path)
    parser.add_argument("photograph", type=Path)
    args = parser.parse_args()
    source_bytes = args.framebuffer.read_bytes()
    photo_bytes = args.photograph.read_bytes()
    if len(source_bytes) != 320 * 240 * 2 or len(photo_bytes) != 0xA000:
        parser.error("expected 153600 framebuffer bytes and 40960 photograph bytes")
    source = struct.unpack(">76800H", source_bytes)
    photo = struct.unpack(">20480H", photo_bytes)
    mismatches = 0
    samples = []
    for ty in range(2):
        for tx in range(5):
            for y in range(64):
                for x in range(32):
                    index = ((ty * 5 + tx) * 64 + y) * 32 + x
                    value = source[(56 + ty * 64 + y) * 320 + 80 + tx * 32 + x]
                    expected = sepia(value)
                    if photo[index] != expected:
                        mismatches += 1
                        if len(samples) < 8:
                            samples.append({"index": index, "source": value,
                                            "expected": expected, "actual": photo[index]})
    print(json.dumps({
        "framebuffer_sha256": hashlib.sha256(source_bytes).hexdigest(),
        "photograph_sha256": hashlib.sha256(photo_bytes).hexdigest(),
        "pixels": len(photo), "matches": len(photo) - mismatches,
        "mismatches": mismatches, "samples": samples,
    }, indent=2))
    return int(mismatches != 0)


if __name__ == "__main__":
    raise SystemExit(main())
