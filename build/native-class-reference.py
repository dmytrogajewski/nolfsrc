#!/usr/bin/env python3
"""Read static ClassDef metadata from a locally owned Win32 Object.lto.

This is a reference-format audit, never a loader: no image code is executed.
The original SDK v1 ClassDef is 44 bytes. Property arrays may be in .bss and
initialized by the DLL startup code, so only their mapped extent is checked.
The output contains class names, parents, flags and declared property counts.
"""
import argparse
from pathlib import Path
import re
import struct


def read_classes(data):
    def words(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError("truncated reference image")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("expected an original PE image")
    pe, = words("<I", 0x3C)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    machine, count = words("<HH", pe + 4)
    optional_size, = words("<H", pe + 20)
    optional = pe + 24
    magic, = words("<H", optional)
    if machine != 0x14C or magic != 0x10B or optional_size < 96 or not 1 <= count <= 96:
        raise ValueError("expected a Win32 SDK v1 reference")
    base, = words("<I", optional + 28)
    sections = []
    for index in range(count):
        offset = optional + optional_size + index * 40
        virtual_size, address, size, raw = words("<4I", offset + 8)
        flags, = words("<I", offset + 36)
        if raw + size > len(data) or base + address + max(size, virtual_size) > 1 << 32:
            raise ValueError("reference section outside image")
        sections.append((base + address, raw, size, max(size, virtual_size), flags))

    def raw_offset(address):
        for virtual, raw, size, _extent, _flags in sections:
            if virtual <= address < virtual + size:
                return raw + address - virtual
        return None

    def mapped(address):
        return any(virtual <= address < virtual + extent for virtual, _raw, _size, extent, _flags in sections)

    def executable(address):
        return not address or any(virtual <= address < virtual + extent and flags & 0x20000000
                                  for virtual, _raw, _size, extent, flags in sections)

    def name_at(address):
        offset = raw_offset(address)
        if offset is None:
            return None
        end = data.find(b"\0", offset, offset + 128)
        if end < 0 or not re.fullmatch(rb"[A-Za-z_][A-Za-z0-9_]*", data[offset:end]):
            return None
        return data[offset:end].decode("ascii")

    records = {}
    for virtual, raw, size, _extent, flags in sections:
        if not flags & 0x80000000:
            continue
        for offset in range(raw, raw + size - 43, 4):
            name, parent, class_flags, construct, destroy, plugin, count, props, object_size, internal0, internal1 = words("<6Ih2x4I", offset)
            name = name_at(name)
            if not name or class_flags & ~15 or not 8 <= object_size <= 1024 * 1024 or not 0 <= count <= 4096 or internal0 or internal1:
                continue
            if not all(executable(pointer) for pointer in (construct, destroy, plugin)):
                continue
            if parent and not mapped(parent) or count and not mapped(props):
                continue
            records[virtual + offset - raw] = (name, parent, class_flags, count)
    if not records:
        raise ValueError("no original SDK class records found")
    names = {}
    for address, (name, parent, flags, count) in records.items():
        if name in names or parent and parent not in records:
            raise ValueError("ambiguous or incomplete class graph")
        seen = {address}
        cursor = parent
        while cursor:
            if cursor in seen or cursor not in records:
                raise ValueError("cyclic or invalid reference class graph")
            seen.add(cursor)
            cursor = records[cursor][1]
        names[name] = (records[parent][0] if parent else "-", flags, count)
    if "BaseClass" not in names:
        raise ValueError("reference has no BaseClass")
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    records = read_classes(args.reference.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("NOLF CLASS REFERENCE 1\n" + "".join(
        f"{name}\t{parent}\t{flags}\t{count}\n" for name, (parent, flags, count) in sorted(records.items())), encoding="utf-8")
    print(f"Validated {len(records)} original Windows class definitions without executing the image")


if __name__ == "__main__":
    main()
