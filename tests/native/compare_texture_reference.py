#!/usr/bin/env python3
"""Manual, asset-dependent DTX pixel comparison against Pillow's DDS decoder.

The native test exports its original inputs and per-mip BGRA CRC32 report.
This reader parses DTX independently and wraps compressed blocks in DDS;
it never imports the native codec or rewrites the supplied game data.
"""
import argparse
from collections import defaultdict
from io import BytesIO
from pathlib import Path
import struct
import zlib

from PIL import Image


def decode(file):
    resource, version, width, height, levels, sections, flags = struct.unpack_from("<Ii4HI", file)
    if resource != 0 or version != -5:
        raise ValueError("not a version -5 DTX")
    format = file[26] or 3
    position = 164
    encoded = []
    for _ in range(levels):
        if format == 3:
            size = width * height * 4
        elif format == 7:
            size = width * height
        else:
            size = ((width + 3) // 4) * ((height + 3) // 4) * (8 if format == 4 else 16)
        encoded.append((width, height, file[position:position + size]))
        position += size
        width //= 2
        height //= 2
    palette = None
    if flags & 8:
        for _ in range(sections):
            kind = file[position:position + 15].split(b"\0")[0]
            size, = struct.unpack_from("<I", file, position + 28)
            position += 32
            if kind == b"PALLETE32":
                palette = file[position:position + size]
            position += size
    for width, height, data in encoded:
        if format == 3:
            image = Image.frombytes("RGBA", (width, height), data, "raw", "BGRA")
        elif format == 7:
            rgba = bytearray()
            for blue, green, red, alpha in struct.iter_unpack("<4B", palette[:1024]):
                rgba.extend((red, green, blue, alpha))
            image = Image.frombytes("P", (width, height), data)
            image.putpalette(bytes(rgba), rawmode="RGBA")
            image = image.convert("RGBA")
        else:
            fourcc = {4: b"DXT1", 5: b"DXT3", 6: b"DXT5"}[format]
            header = b"DDS " + struct.pack("<7I", 124, 0x81007, height, width, len(data), 0, 0)
            header += bytes(44) + struct.pack("<II4s5I", 32, 4, fourcc, 0, 0, 0, 0, 0)
            header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
            image = Image.open(BytesIO(header + data)).convert("RGBA")
        yield width, height, image.tobytes("raw", "BGRA")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("--mismatches", type=Path)
    args = parser.parse_args()
    expected = defaultdict(dict)
    for line in args.report.read_text().splitlines():
        name, mip, width, height, crc = line.split("\t")
        expected[name][int(mip)] = (int(width), int(height), int(crc))
    mismatches = []
    count = 0
    for name, mips in expected.items():
        levels = list(decode((args.originals / name).read_bytes()))
        if len(levels) != len(mips):
            raise ValueError(f"{name}: different mip count")
        for mip, (width, height, pixels) in enumerate(levels):
            actual = (width, height, zlib.crc32(pixels))
            if actual != mips[mip]:
                mismatches.append((name, mip, mips[mip], actual))
            count += 1
    if args.mismatches:
        args.mismatches.write_text("".join(f"{name}\t{mip}\t{expected}\t{actual}\n" for name, mip, expected, actual in mismatches))
    for mismatch in mismatches[:12]:
        print("Mismatch:", *mismatch)
    print(f"Pillow comparison: {len(expected)} DTX files, {count} mip images, {len(mismatches)} mismatches")
    return bool(mismatches)


if __name__ == "__main__":
    raise SystemExit(main())
