#!/usr/bin/env python3
"""Keep migrated native sources behind their platform adapters.

Audit the actual native compilation database, not historical Windows targets.
An inventory of the remaining legacy dependencies is emitted separately.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import sys

ROOT = Path(__file__).resolve().parents[1]
OS_HEADERS = re.compile(r'#\s*include\s*[<"](?:windows\.h|winsock\d?\.h|ws2tcpip\.h|mmsystem\.h|dinput\.h|d3d\d*\.h|ddraw\.h|dmusic\w*\.h|afx\w*\.h|SDL\d?/|dlfcn\.h|unistd\.h|sys/socket\.h|arpa/inet\.h|netdb\.h|poll\.h)', re.I)
OS_BRANCH = re.compile(r'^\s*#\s*(?:if|ifdef|ifndef|elif)\b[^\n]*(?:_WIN32|WIN32|__linux__|__APPLE__|_MSC_VER)', re.M)
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.M)


def dependencies(compilation):
    """Follow project headers too, so indirect OS includes cannot evade audit."""
    found = set()
    for entry in compilation:
        source = Path(entry["file"]).resolve()
        if not source.is_relative_to(ROOT / "src"):
            continue
        arguments = entry.get("arguments") or shlex.split(entry["command"], posix=True)
        directories = []
        for index, argument in enumerate(arguments):
            if argument in ("-I", "/I") and index + 1 < len(arguments):
                argument += arguments[index + 1]
            if argument.startswith(("-I", "/I")) and len(argument) > 2:
                directory = Path(argument[2:])
                if not directory.is_absolute():
                    directory = Path(entry["directory"]) / directory
                directories.append(directory)
        pending = [source]
        visited = set()
        while pending:
            path = pending.pop().resolve()
            if path in visited or not path.is_relative_to(ROOT / "src"):
                continue
            visited.add(path)
            found.add(path)
            # The adapter owns its transitive OS dependencies as well.
            if path.is_relative_to(ROOT / "src/platform"):
                continue
            for name in INCLUDE.findall(path.read_text(errors="replace")):
                for directory in [path.parent, *directories]:
                    include = directory / name
                    if include.is_file():
                        pending.append(include)
                        break
    return sorted(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compilation_database", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    compilation = json.loads(args.compilation_database.read_text())
    sources = sorted({Path(entry["file"]).resolve() for entry in compilation})
    violations = []
    headers = dependencies(compilation)
    for source in headers:
        if not source.is_relative_to(ROOT / "src") or source.is_relative_to(ROOT / "src/platform"):
            continue
        text = source.read_text()
        for pattern in (OS_HEADERS, OS_BRANCH):
            for match in pattern.finditer(text):
                violations.append(f"{source.relative_to(ROOT)}:{text.count(chr(10), 0, match.start()) + 1}: {match.group().strip()}")
    inventory = []
    for source in (ROOT / "src").rglob("*"):
        if source.suffix not in (".cpp", ".c", ".h") or source.is_relative_to(ROOT / "src/platform"):
            continue
        text = source.read_text(errors="replace")
        if OS_HEADERS.search(text):
            inventory.append(str(source.relative_to(ROOT)))
    report = {"native_sources": [str(p.relative_to(ROOT)) for p in sources if p.is_relative_to(ROOT / "src")],
              "native_dependencies": [str(p.relative_to(ROOT)) for p in headers],
              "violations": violations, "legacy_files_with_os_headers": inventory}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    for violation in violations:
        print(violation, file=sys.stderr)
    print(f"{len(report['native_sources'])} native sources audited; {len(violations)} boundary violations; {len(inventory)} legacy files still contain OS headers")
    return bool(violations)


if __name__ == "__main__":
    sys.exit(main())
