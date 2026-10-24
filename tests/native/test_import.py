#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
import json

spec = importlib.util.spec_from_file_location("import_assets", Path(__file__).resolve().parents[2] / "build/import-assets.py")
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


def archive(payload=b"hello", name=b"EXAMPLE", offset=139, extension=b"TXT\0"):
    root = offset + len(payload)
    entry = struct.pack("<5I", 0, offset, len(payload), 0, 0) + extension + struct.pack("<I", 0) + name + b"\0\0"
    return b"RezMgr".ljust(127, b" ") + struct.pack("<III", 1, root, len(entry)) + payload + entry


def pe_image():
    label = "NOLF".encode("utf-16le")
    payload = struct.pack("<H", 4) + label + bytes(15 * 2)
    resource = bytearray(96 + len(payload))
    for offset in (0, 24, 48):
        struct.pack_into("<H", resource, offset + 14, 1)
    struct.pack_into("<II", resource, 16, 6, 0x80000000 | 24)
    struct.pack_into("<II", resource, 40, 1, 0x80000000 | 48)
    struct.pack_into("<II", resource, 64, 1033, 80)
    struct.pack_into("<IIII", resource, 80, 0x1000 + 96, len(payload), 0, 0)
    resource[96:] = payload
    image = bytearray(512 + len(resource))
    image[:2] = b"MZ"
    struct.pack_into("<I", image, 0x3c, 0x80)
    image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", image, 0x86, 1)
    struct.pack_into("<H", image, 0x94, 224)
    optional = 0x80 + 24
    struct.pack_into("<H", image, optional, 0x10b)
    struct.pack_into("<II", image, optional + 112, 0x1000, len(resource))
    section = optional + 224
    struct.pack_into("<IIII", image, section + 8, len(resource), 0x1000, len(resource), 512)
    image[512:] = resource
    return bytes(image)


class ImportTests(unittest.TestCase):
    def test_paths(self):
        self.assertEqual(assets.asset_key("Worlds\\M01S01.DAT"), "worlds/m01s01.dat")
        for name in ("../a", "C:\\a", "\\a", "a\0b", "/a", "."):
            with self.assertRaises(ValueError):
                assets.asset_key(name)

    def test_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "test.rez"
            path.write_bytes(archive())
            rez = assets.Rez(path)
            self.assertEqual(rez.read(*rez.entries["example.txt"]), b"hello")
            path.write_bytes(archive()[:-1])
            with self.assertRaises(ValueError):
                assets.Rez(path)

    def test_import_excludes_binaries(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / "game"
            game.mkdir()
            for name in ("NOLF.REZ", "NOLF2.REZ", "NOLFU003.REZ", "NOLFGOTY.REZ"):
                (game / name).write_bytes(archive(b"MZbinary"))
            assets.import_directory(game, root / "output", "english")
            rez = assets.Rez(root / "output/nolf.rez")
            self.assertEqual(rez.read(*rez.entries["example.txt"]), bytes(8))
            self.assertEqual((game / "NOLF.REZ").read_bytes()[139:147], b"MZbinary")

    def test_pe_resource_export(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertEqual(assets.export_resources(pe_image(), "cres", root), 1)
            self.assertEqual((root / "resources/cres/1033/strings/0.txt").read_text(), "NOLF")
            with self.assertRaises(ValueError):
                list(assets.pe_resources(pe_image()[:-1]))

    def test_retail_client_identity_survives_executable_removal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / "game"
            game.mkdir()
            for i, name in enumerate(("NOLF.REZ", "NOLF2.REZ", "NOLFU003.REZ", "NOLFGOTY.REZ")):
                payload = pe_image() + bytes([i]) * 16
                (game / name).write_bytes(archive(payload, b"CSHELL", extension=b"LLD\0"))
            assets.import_directory(game, root / "output", "english")
            metadata = (root / "output/native/compatibility/cshell.dll").read_bytes()
            self.assertEqual(metadata, b"NLFCMP01" + struct.pack("<II",len(payload),zlib.crc32(payload)))
            self.assertEqual((root / "output/resources/cshell/1033/strings/0.txt").read_text(), "NOLF")
            identity = json.loads((root / "output/assets.json").read_text())["compatibility_identities"]["cshell.dll"]
            self.assertEqual(identity["archive"], "nolfgoty.rez")
            for name in ("nolf.rez", "nolf2.rez", "nolfu003.rez", "nolfgoty.rez"):
                rez = assets.Rez(root / "output" / name)
                self.assertEqual(rez.read(*rez.entries["cshell.dll"]), bytes(len(payload)))


if __name__ == "__main__":
    unittest.main()
