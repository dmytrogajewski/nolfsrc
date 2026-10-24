#!/usr/bin/env python3
"""The headless host rejects invalid configuration before touching game data."""
import subprocess
import sys

executable = sys.argv[1]


def run(arguments):
    return subprocess.run([executable, *arguments], capture_output=True, text=True, timeout=15)


help_result = run(["--help"])
assert help_result.returncode == 0 and "Usage: nolf-server" in help_result.stdout
assert "--world" in help_result.stdout and "SIGTERM" in help_result.stdout
for arguments, diagnostic in [
    ([], "are required"),
    (["--port"], "missing value"),
    (["--port", "0"], "1..65535"),
    (["--port", "65536"], "1..65535"),
    (["--port", "-1"], "1..65535"),
    (["--port", "123x"], "1..65535"),
    (["--players", "129"], "1..128"),
    (["--ticks", "0"], "1..604800"),
    (["--mode", "invalid"], "dm or assault"),
    (["--bind", "localhost"], "numeric IPv4"),
    (["--bind", "127.0.0.999"], "numeric IPv4"),
    (["--bind", "127"], "numeric IPv4"),
    (["--name", "x" * 25], "1..24 bytes"),
    (["--name", "line\nbreak"], "control characters"),
    (["--name", ""], "empty value"),
    (["--world", "../escape"], "traversal"),
    (["--world", "x" * 128], "level asset name"),
    (["--world", "worlds/test"] * 51, "at most 50"),
    (["--unknown", "value"], "unknown option"),
]:
    result = run(arguments)
    assert result.returncode == 1 and diagnostic in result.stderr, (arguments, result.returncode, result.stderr)
print("native server command-line validation passed")
