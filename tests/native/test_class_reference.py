#!/usr/bin/env python3
"""Independent PE32 fixtures for the read-only original class reference audit."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("class_reference", Path(__file__).resolve().parents[2] / "build/native-class-reference.py")
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)


def image():
    data = bytearray(0x800)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x14C, 3)
    struct.pack_into("<H", data, 0x94, 224)
    struct.pack_into("<H", data, 0x98, 0x10B)
    struct.pack_into("<I", data, 0xB4, 0x400000)
    for index, (name, va, raw, raw_size, virtual_size, flags) in enumerate((
            (b".text", 0x1000, 0x200, 0x100, 0x100, 0x60000020),
            (b".rdata", 0x2000, 0x400, 0x100, 0x100, 0x40000040),
            (b".data", 0x3000, 0x600, 0x200, 0x400, 0xC0000040))):
        section = 0x178 + 40 * index
        data[section:section + 8] = name.ljust(8, b"\0")
        struct.pack_into("<4I", data, section + 8, virtual_size, va, raw_size, raw)
        struct.pack_into("<I", data, section + 36, flags)
    data[0x400:0x40A] = b"BaseClass\0"
    data[0x410:0x416] = b"Actor\0"
    # The property arrays are mapped zero-initialized memory, outside raw data.
    struct.pack_into("<6Ih2x4I", data, 0x600, 0x402000, 0, 0, 0x401000, 0x401004, 0, 3, 0x403300, 16, 0, 0)
    struct.pack_into("<6Ih2x4I", data, 0x630, 0x402010, 0x403000, 4, 0x401008, 0x40100C, 0, 1, 0x403350, 32, 0, 0)
    return data


class ClassReferenceTests(unittest.TestCase):
    def test_original_layout_and_bss_properties(self):
        self.assertEqual(reference.read_classes(image()), {"Actor": ("BaseClass", 4, 1), "BaseClass": ("-", 0, 3)})

    def test_truncation_and_wrong_architecture(self):
        data = image()
        for length in (0, 0x40, 0x90, 0x178, 0x700, len(data) - 1):
            with self.subTest(length=length), self.assertRaises(ValueError):
                reference.read_classes(data[:length])
        struct.pack_into("<H", data, 0x84, 0x8664)
        with self.assertRaises(ValueError):
            reference.read_classes(data)

    def test_cyclic_incomplete_and_ambiguous_graphs(self):
        for field, value in ((0x604, 0x403030), (0x634, 0x403100), (0x630, 0x402000)):
            data = image()
            struct.pack_into("<I", data, field, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                reference.read_classes(data)


if __name__ == "__main__":
    unittest.main()
