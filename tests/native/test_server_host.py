#!/usr/bin/env python3
"""Local, asset-dependent native host/UDP/shutdown check; never contacts a master."""
import argparse
from pathlib import Path
import signal
import socket
import struct
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable", type=Path)
parser.add_argument("module", type=Path)
parser.add_argument("data", type=Path)
parser.add_argument("--logs", type=Path, required=True)
args = parser.parse_args()
args.logs.mkdir(parents=True, exist_ok=True)
guid = struct.pack("<IHH8B", 0x8AFA62A1, 0x1AE, 0x11D5, 0xB9, 0x5C, 0, 0x60, 0x97, 9, 0x83, 0xE)


def read(path):
    return path.read_text(errors="replace")


def receive(peer, predicate, timeout=3):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        peer.settimeout(max(0.001, end-time.monotonic()))
        packet, _ = peer.recvfrom(65535)
        if predicate(packet):
            return packet
    raise AssertionError("expected UDP response missing")


def check(stop_signal):
    label = signal.Signals(stop_signal).name
    path = args.logs / f"host-{label}.log"
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    command = [str(args.executable.resolve()), "--module", str(args.module.resolve()),
               "--data", str(args.data.resolve()), "--world", "worlds/multi/deathmatch/001thepad_dm",
               "--bind", "127.0.0.1", "--port", str(port), "--name", "NativeHostCheck", "--players", "2"]
    with path.open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            end = time.monotonic() + 30
            while "Hosting \"NativeHostCheck\"" not in read(path):
                assert process.poll() is None, read(path)[-6000:]
                assert time.monotonic() < end, "host startup timed out"
                time.sleep(.025)
            assert f"127.0.0.1:{port} (2 players)" in read(path)
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
                peer.bind(("127.0.0.1", 0))
                peer.sendto(b"\x00\x01"+guid+b"\x12", ("127.0.0.1", port))
                assert receive(peer, lambda packet: packet.startswith(b"\x00\x02")) == b"\x00\x02NativeHostCheck\x00\x12"
                peer.sendto(b"\\basic\\", ("127.0.0.1", port))
                reply = receive(peer, lambda packet: packet.startswith(b"\\"))
                assert b"\\gamename\\nolf" in reply and b"\\final\\" in reply, reply
                # A join with the wrong application GUID is rejected by the
                # actual host. The accepted peer below exercises transport
                # admission only; it does not perform the game handshake.
                peer.sendto(b"\x00\x03\x00"+bytes(16), ("127.0.0.1", port))
                receive(peer, lambda packet: packet[:2] == b"\x00\x07")
                peer.sendto(b"\x00\x03\x00"+guid, ("127.0.0.1", port))
                receive(peer, lambda packet: packet[:2] == b"\x00\x05")
                for malformed in (b"\x07",b"\x07\x1e\x01\xff",b"\x08\x01",b"\x09",b"\x09\xff",b"\x0b"):
                    peer.sendto(malformed,("127.0.0.1",port))
                peer.sendto(b"\x00\x01"+guid+b"\x13",("127.0.0.1",port))
                assert receive(peer, lambda packet: packet.startswith(b"\x00\x02")) == b"\x00\x02NativeHostCheck\x00\x13"
                peer.sendto(b"\x00\x00"+struct.pack("<I",0), ("127.0.0.1", port))
                time.sleep(.1)
            # Failure to bind a second process must unwind its partially
            # initialized server without terminating the original listener.
            conflict = subprocess.run(command+["--ticks","1"], capture_output=True, timeout=30)
            assert conflict.returncode == 1 and b"cannot host UDP port" in conflict.stderr, conflict.stderr[-3000:]
            assert process.poll() is None
            process.send_signal(stop_signal)
            assert process.wait(timeout=15) == 0, read(path)[-6000:]
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
    output = read(path)
    assert "Stopping server after" in output and "World ended" in output
    assert "AddressSanitizer" not in output and "runtime error:" not in output and "LeakSanitizer" not in output
    # Normal destruction releases the actual listener, allowing immediate reuse.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as released:
        released.bind(("127.0.0.1", port))
    print(f"{label}: original deathmatch world, discovery, GameSpy query, GUID rejection, transport admission, malformed packets, bind conflict and shutdown passed")


check(signal.SIGTERM)
check(signal.SIGINT)
