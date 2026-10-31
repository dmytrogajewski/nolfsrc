import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from moddata import edit_world_objects


def string(value):
    raw = value.encode("latin-1")
    return struct.pack("<H", len(raw)) + raw


def actor(name, sound):
    record = string("SoundFX") + struct.pack("<I", 2)
    for key, value in [("Name", name), ("Sound", sound)]:
        payload = string(value)
        record += string(key) + struct.pack("<BIH", 0, 0, len(payload)) + payload
    return struct.pack("<H", len(record)) + record


class WorldEdits(unittest.TestCase):
    def setUp(self):
        section = struct.pack("<I", 2) + actor("water", "typo.wav") + actor("orphan", "unused.wav")
        self.raw = struct.pack("<11I", 66, 44, 44 + len(section), *([0] * 8)) + section + b"render data unchanged"
        self.edits = [dict(name="water", **{"class": "SoundFX"}, property="Sound", old="typo.wav", new="recorded/water.wav"),
                      dict(name="orphan", **{"class": "SoundFX"}, remove=True)]

    def test_resize_remove_and_relocate(self):
        output = edit_world_objects(self.raw, self.edits, "fixture")
        self.assertEqual(struct.unpack_from("<I", output, 44)[0], 1)
        self.assertIn(string("recorded/water.wav"), output)
        self.assertNotIn(string("orphan"), output)
        render = struct.unpack_from("<I", output, 8)[0]
        self.assertEqual(output[render:], b"render data unchanged")
        # Applying the same transformation again must reject changed input.
        with self.assertRaises(ValueError):
            edit_world_objects(output, self.edits, "fixture")

    def test_reject_drift_and_ambiguous_match(self):
        self.edits[0]["old"] = "different.wav"
        with self.assertRaises(ValueError):
            edit_world_objects(self.raw, self.edits, "fixture")
        self.edits[0]["old"] = "typo.wav"
        self.edits[0]["name"] = "missing"
        with self.assertRaises(ValueError):
            edit_world_objects(self.raw, self.edits, "fixture")


if __name__ == "__main__":
    unittest.main()
