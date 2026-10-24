#!/usr/bin/env python3
"""Import locally owned NOLF data. Never downloads data or executes game code.

  python3 build/import-assets.py exclude/NOLF_CD.iso --output out/native-data
  python3 build/import-assets.py /path/to/installed/NOLF --output out/native-data

ISO imports need bsdtar and innoextract. ArC payloads additionally need unarc
(--unarc PATH or UNARC). Only the selected localization is imported.
"""
from __future__ import annotations

import argparse
import importlib.util
import hashlib
import json
import re
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib


def asset_key(name: str) -> str:
    name = name.replace("\\", "/")
    parts = name.split("/")
    if not name or name.startswith("/") or ":" in name or "\0" in name or ".." in parts:
        raise ValueError(f"invalid asset path: {name!r}")
    key = "/".join(p for p in parts if p and p != ".")
    if not key:
        raise ValueError("empty asset path")
    return key.translate(str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


class Rez:
    """Bounded REZ metadata reader; entry payloads stay on disk."""
    def __init__(self, path: Path):
        self.path = path
        self.size = path.stat().st_size
        self.entries: dict[str, tuple[int, int]] = {}
        self.visited: set[int] = set()
        with path.open("rb") as stream:
            header = stream.read(139)
        if len(header) != 139 or b"RezMgr" not in header[:127]:
            raise ValueError(f"not a REZ archive: {path}")
        version, offset, size = struct.unpack_from("<III", header, 127)
        if version != 1:
            raise ValueError(f"unsupported REZ version {version}")
        self._directory(offset, size, "", 0)

    def read(self, offset: int, size: int) -> bytes:
        if offset > self.size or size > self.size - offset:
            raise ValueError(f"REZ extent outside archive: {self.path}")
        with self.path.open("rb") as stream:
            stream.seek(offset)
            data = stream.read(size)
        if len(data) != size:
            raise ValueError("truncated REZ data")
        return data

    def _directory(self, offset: int, size: int, prefix: str, depth: int):
        if depth > 64 or offset in self.visited or size > 64 * 1024 * 1024:
            raise ValueError("invalid REZ directory tree")
        self.visited.add(offset)
        data = self.read(offset, size)
        cursor = 0
        children = []

        def word():
            nonlocal cursor
            if cursor + 4 > len(data):
                raise ValueError("truncated REZ directory")
            value, = struct.unpack_from("<I", data, cursor)
            cursor += 4
            return value

        def string():
            nonlocal cursor
            end = data.find(b"\0", cursor)
            if end < 0:
                raise ValueError("unterminated REZ name")
            value = data[cursor:end].decode("latin-1")
            cursor = end + 1
            return value

        while cursor < len(data):
            kind, pos, length, _time = word(), word(), word(), word()
            if kind == 1:
                component = string()
                if "/" in component or "\\" in component:
                    raise ValueError("invalid REZ directory name")
                children.append((pos, length, prefix + asset_key(component) + "/"))
            elif kind == 0:
                word()  # resource ID
                if cursor + 4 > len(data):
                    raise ValueError("truncated REZ type")
                extension = data[cursor:cursor + 4].rstrip(b"\0")[::-1].decode("latin-1")
                cursor += 4
                keys = word()
                name = string()
                cursor += 1 + keys * 4
                if cursor > len(data):
                    raise ValueError("truncated REZ keys")
                key = asset_key(prefix + name + ("." + extension if extension else ""))
                if key in self.entries or pos > self.size or length > self.size - pos:
                    raise ValueError("duplicate or invalid REZ entry")
                self.entries[key] = pos, length
            else:
                raise ValueError(f"invalid REZ record type {kind}")
        for pos, length, name in children:
            self._directory(pos, length, name, depth + 1)


def run(*args: str | Path):
    subprocess.run([str(arg) for arg in args], check=True)


def tool(name: str) -> str:
    found = shutil.which(name)
    if not found:
        raise ValueError(f"asset import needs {name}; install it and rerun this command")
    return found


def pe_resources(data: bytes):
    """Read resource payloads from PE32/PE32+ without loading the image."""
    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError("truncated PE resource data")
        return struct.unpack_from(fmt, data, offset)

    pe, = unpack("<I", 0x3c)
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    sections, = unpack("<H", pe + 6)
    optional_size, = unpack("<H", pe + 20)
    optional = pe + 24
    magic, = unpack("<H", optional)
    directory_offset = {0x10b: 96, 0x20b: 112}.get(magic)
    if directory_offset is None or optional_size < directory_offset + 24:
        raise ValueError("unsupported PE optional header")
    resource_rva, resource_size = unpack("<II", optional + directory_offset + 16)
    if not resource_rva or not resource_size:
        return
    section_table = optional + optional_size

    def rva_file(rva, length):
        for i in range(sections):
            virtual_size, address, raw_size, raw = unpack("<IIII", section_table + i * 40 + 8)
            if address <= rva and rva - address < max(virtual_size, raw_size):
                offset = rva - address
                if offset + length > raw_size or raw + offset + length > len(data):
                    raise ValueError("PE resource outside section")
                return raw + offset
        raise ValueError("PE resource RVA has no section")

    base = rva_file(resource_rva, resource_size)
    visited = set()

    def relative(offset, size):
        if offset < 0 or offset + size > resource_size:
            raise ValueError("PE resource directory outside section")
        return base + offset

    def walk(offset, path):
        if offset in visited or len(path) > 2:
            raise ValueError("invalid PE resource directory tree")
        visited.add(offset)
        named, ids = unpack("<HH", relative(offset, 16) + 12)
        for i in range(named + ids):
            name, child = unpack("<II", relative(offset + 16 + i * 8, 8))
            if name & 0x80000000:
                string_offset = name & 0x7fffffff
                length, = unpack("<H", relative(string_offset, 2))
                start = relative(string_offset + 2, length * 2)
                key = data[start:start + length * 2].decode("utf-16le")
            else:
                key = name
            if child & 0x80000000:
                yield from walk(child & 0x7fffffff, path + (key,))
            else:
                rva, size, _encoding, _reserved = unpack("<IIII", relative(child, 16))
                start = rva_file(rva, size)
                if len(path) != 2:
                    raise ValueError("PE resource is missing type/name/language")
                yield path + (key,), data[start:start + size]
    yield from walk(0, ())


def export_resources(data: bytes, module: str, destination: Path) -> int:
    count = 0
    for (kind, name, language), payload in pe_resources(data):
        # Numeric IDs retain the original resource API's meaning. Named
        # resources use the same safe path normalization as archive entries.
        root = destination / "resources" / asset_key(module) / asset_key(str(language))
        raw = root / "raw" / asset_key(str(kind)) / (asset_key(str(name)) + ".bin")
        raw.parent.mkdir(parents=True, exist_ok=True)
        raw.write_bytes(payload)
        count += 1
        if kind == 6 and isinstance(name, int):  # RT_STRING: sixteen UTF-16 strings per block
            cursor = 0
            for i in range(16):
                if cursor + 2 > len(payload):
                    raise ValueError("truncated string-table resource")
                length, = struct.unpack_from("<H", payload, cursor)
                cursor += 2
                if cursor + length * 2 > len(payload):
                    raise ValueError("truncated resource string")
                text = payload[cursor:cursor + length * 2].decode("utf-16le")
                cursor += length * 2
                if text:
                    string = root / "strings" / f"{(name - 1) * 16 + i}.txt"
                    string.parent.mkdir(parents=True, exist_ok=True)
                    string.write_text(text, encoding="utf-8")
    return count


def unpack_iso(source: Path, scratch: Path, unarc: str | None, language: str) -> Path:
    iso = scratch / "iso"
    iso.mkdir()
    # Check names before handing the archive to the extractor.
    listing = subprocess.check_output([tool("bsdtar"), "-tf", str(source)], text=True)
    for name in listing.splitlines():
        cleaned = name.removeprefix("./").rstrip("/")
        if cleaned and cleaned != ".":
            asset_key(cleaned)
    run(tool("bsdtar"), "-xf", source, "-C", iso)
    setups = sorted(path for path in iso.rglob("*") if path.name.lower() == "setup.exe")
    if not setups:
        return iso
    setup = setups[0]
    extracted = scratch / "inno"
    run(tool("innoextract"), "-d", extracted, setup)
    payloads = sorted(path for path in setup.parent.iterdir() if path.suffix.lower() == ".pak")
    if not payloads:
        return extracted
    unarc = unarc or os.environ.get("UNARC") or shutil.which("unarc")
    local_unarc = Path(__file__).resolve().parents[1] / "out/deps/unarc/unarc"
    if not unarc and local_unarc.is_file():
        unarc = str(local_unarc)
    if not unarc:
        raise ValueError("ArC payloads found; provide a native unarc executable with --unarc PATH")
    game = scratch / "game"
    game.mkdir()
    # This image has the complete English game in data001, with two optional
    # Russian translations in data002/data003. Choose one, never stack both.
    chosen = payloads[:1]
    if language == "russian" and len(payloads) > 1:
        chosen.append(payloads[1])
    for payload in chosen:
        run(unarc, "x", "-o+", "--noarcext", f"-dp{game}", payload)
    if language == "russian":
        for path in extracted.rglob("*"):
            if path.name.lower() == "nolf_ru.rez":
                shutil.copy2(path, game / path.name)
    return game


def import_directory(source: Path, destination: Path, language: str):
    candidates = sorted(path for path in source.rglob("*") if path.is_file() and path.name.lower() == "nolf.rez")
    if len(candidates) != 1:
        raise ValueError(f"expected one installed NOLF.REZ; found {len(candidates)} under {source}")
    root = candidates[0].parent
    index = {}
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"asset symlink is not allowed: {path}")
        if path.is_file():
            key = asset_key(path.relative_to(root).as_posix())
            if key in index:
                raise ValueError(f"case collision: {path} and {index[key]}")
            index[key] = path
    archive_order = ["nolf.rez", "nolf2.rez", "nolfu003.rez", "nolfgoty.rez"]
    missing = [name for name in archive_order if name not in index]
    if missing:
        raise ValueError("incomplete GOTY 1.003 data; missing: " + ", ".join(missing))
    if language == "russian":
        localization = index.get("nolf_ru.rez")
        # Optional translations can be placed beside the installed directory.
        if not localization:
            localization = next((p for p in source.rglob("*") if p.name.lower() == "nolf_ru.rez"), None)
        if not localization:
            raise ValueError("Russian localization requested but NOLF_RU.REZ is absent")
        index["nolf_ru.rez"] = localization
        archive_order.append("nolf_ru.rez")
    destination.mkdir(parents=True, exist_ok=True)
    records = []
    resources = {}
    binary_entries = []
    resource_modules = {}
    compatibility = {}
    for name in archive_order:
        archive = Rez(index[name])
        target = destination / name
        shutil.copyfile(index[name], target)
        with target.open("r+b") as output:
            for entry, (offset, size) in archive.entries.items():
                resources[entry] = name
                if entry == "cshell.dll":
                    # Preserve the retail wire identity before executable data
                    # is removed. Native ELF modules cannot have the PE CRC.
                    data = archive.read(offset, size)
                    if not data.startswith(b"MZ"):
                        raise ValueError(f"client shell is not a PE image in {name}")
                    compatibility[entry] = {"archive": name, "size": size,
                        "crc32": zlib.crc32(data), "sha256": hashlib.sha256(data).hexdigest()}
                if size >= 2 and archive.read(offset, 2) == b"MZ":
                    if entry in ("cres.dll", "sres.dll", "ltmsg.dll", "cshell.dll"):
                        resource_modules[Path(entry).stem] = archive.read(offset, size)
                    binary_entries.append({"archive": name, "name": entry, "size": size})
                    output.seek(offset)
                    remaining = size
                    while remaining:
                        block = min(remaining, 1024 * 1024)
                        output.write(bytes(block))
                        remaining -= block
        records.append({"path": name, "source_sha256": sha256(index[name]), "sha256": sha256(target), "entries": len(archive.entries)})
    for entry, identity in compatibility.items():
        target = destination / "native/compatibility" / entry
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b"NLFCMP01" + struct.pack("<II", identity["size"], identity["crc32"]))
        records.append({"path": target.relative_to(destination).as_posix(), "sha256": sha256(target)})
    for key, path in index.items():
        if key in ("lithtech.exe", "ltmsg.dll"):
            resource_modules[Path(key).stem] = path.read_bytes()
        if key.endswith((".bik", ".smk", ".pcx", ".ttf")) or key in ("defaults.cfg", "defctrls.cfg"):
            target = destination / key
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            records.append({"path": key, "sha256": sha256(target)})
    worlds = sorted(key for key in resources if key.startswith("worlds/") and key.endswith(".dat"))
    music = sorted(key for key in resources if key.endswith((".sgt", ".sty", ".dls", ".ctl")))
    resource_count = sum(export_resources(data, module, destination) for module, data in resource_modules.items())
    manifest = {
        "schema": 1, "game": "NOLF GOTY", "compatibility_target": "1.003",
        "language": language, "archive_order": archive_order,
        "files": records, "worlds": worlds, "music": music,
        "excluded_binary_entries": binary_entries,
        "compatibility_identities": compatibility,
        "exported_resources": resource_count,
        "source": str(source.resolve()),
    }
    (destination / "assets.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (destination / "archives.txt").write_text("\n".join(archive_order) + "\n")
    print(f"Imported {len(worlds)} worlds, {len(music)} music resources; excluded {len(binary_entries)} executable entries")


REPOSITORY = Path(__file__).resolve().parents[1]


def rc_text(literal: str) -> str:
    """An RC string literal's contents: doubled quotes and C escapes."""
    out, i = [], 0
    while i < len(literal):
        c = literal[i]
        if c == '"' and literal[i + 1:i + 2] == '"':
            out.append('"'); i += 2; continue
        if c == "\\" and i + 1 < len(literal):
            nxt = literal[i + 1]
            out.append({"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"'}.get(nxt, nxt)); i += 2; continue
        out.append(c); i += 1
    return "".join(out)


def export_modern_strings(destination: Path) -> int:
    """The modern build's string tables (src/modern/cres), which it compiles
    into CRes.dll beside the retail strings: written as cres strings."""
    source = REPOSITORY / "src" / "modern" / "cres"
    ids = {}
    for header in sorted(source.glob("*.h")):
        for name, value in re.findall(r"^\s*#define\s+(IDS_\w+)\s+(\d+)", header.read_text(encoding="latin-1"), re.M):
            ids[name] = int(value)
    count = 0
    root = destination / "resources" / "cres" / "1033" / "strings"
    root.mkdir(parents=True, exist_ok=True)
    for table in sorted(source.glob("*.rc")):
        for name, literal in re.findall(r'^\s*(IDS_\w+)\s+"((?:[^"]|"")*)"\s*$', table.read_text(encoding="latin-1"), re.M):
            if name not in ids:
                raise ValueError(f"{table.name}: {name} has no ID in the package headers")
            (root / f"{ids[name]}.txt").write_bytes(rc_text(literal).encode("latin-1"))
            count += 1
    return count


def apply_modern(destination: Path) -> None:
    """The modern build's string and game-data edits, which native builds
    (always NOLFSRC_MODERN) expect: data edits go into custom/, searched
    after the archives."""
    strings = export_modern_strings(destination)
    spec = importlib.util.spec_from_file_location("moddata", REPOSITORY / "tools" / "moddata.py")
    moddata = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(moddata)
    moddata.cmd_apply(str(destination), str(REPOSITORY / "src" / "modern" / "data"))
    print(f"Applied the modern build's data edits and {strings} strings")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--update-modern", type=Path, metavar="DATA",
                        help="apply the modern build's edits to an existing import in place")
    parser.add_argument("--unarc")
    parser.add_argument("--language", choices=("english", "russian"), default="english")
    args = parser.parse_args()
    if args.update_modern:
        try:
            apply_modern(args.update_modern.resolve())
            return 0
        except (ValueError, OSError, SystemExit) as error:
            print(f"modern data update failed: {error}", file=sys.stderr)
            return 1
    if args.source is None or args.output is None:
        parser.error("source and --output are required")
    source, destination = args.source.resolve(), args.output.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        parser.error("source and output must be separate directories")
    if destination.exists():
        parser.error("output already exists; choose a new output directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix=".nolf-import-", dir=destination.parent) as temporary:
            scratch = Path(temporary)
            root = source if source.is_dir() else unpack_iso(source, scratch, args.unarc, args.language)
            staged = scratch / "data"
            import_directory(root, staged, args.language)
            apply_modern(staged)
            if source.is_file():
                manifest_path = staged / "assets.json"
                manifest = json.loads(manifest_path.read_text())
                manifest["source"] = str(source)
                manifest["source_sha256"] = sha256(source)
                manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
            staged.rename(destination)
        print(f"Manifest: {destination / 'assets.json'}")
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"asset import failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
