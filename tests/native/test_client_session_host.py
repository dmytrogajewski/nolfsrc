#!/usr/bin/env python3
"""Asset-dependent native engine and original game authentication; no rendered client claim."""
import argparse
from pathlib import Path
import signal
import socket
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("server", type=Path)
parser.add_argument("client", type=Path)
parser.add_argument("module", type=Path)
parser.add_argument("data", type=Path)
parser.add_argument("--logs", type=Path, required=True)
parser.add_argument("--trace", action="store_true")
args = parser.parse_args()
args.logs.mkdir(parents=True, exist_ok=True)
host_log = args.logs / "server.log"
client_log = args.logs / "client.log"
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]
command = [str(args.server.resolve()), "--module", str(args.module.resolve()), "--data", str(args.data.resolve()),
           "--world", "worlds/multi/deathmatch/001thepad_dm", "--bind", "127.0.0.1", "--port", str(port)]
if args.trace:
    config = args.logs / "trace.cfg"
    config.write_text("TransportDebug 2\n")
    command.extend(["--config",str(config.resolve())])
with host_log.open("wb") as host_output, client_log.open("wb") as client_output:
    host = subprocess.Popen(command, stdout=host_output, stderr=subprocess.STDOUT)
    try:
        end = time.monotonic()+30
        while "Hosting " not in host_log.read_text(errors="replace"):
            assert host.poll() is None, host_log.read_text(errors="replace")[-6000:]
            assert time.monotonic() < end, "native host startup timed out"
            time.sleep(.025)
        client_command = [str(args.client.resolve()), "--connect", f"127.0.0.1:{port}", "--data", str(args.data.resolve())]
        if args.trace:
            client_command.append("--trace")
        for flag, reason in (("--bad-weapon", "invalid weapon file"), ("--bad-shell", "invalid client shell")):
            failure_log = args.logs / (flag[2:] + ".log")
            with failure_log.open("wb") as failure_output:
                failed = subprocess.run(client_command + [flag], stdout=failure_output, stderr=subprocess.STDOUT, timeout=40)
            failure_text = failure_log.read_text(errors="replace")
            assert failed.returncode == 1 and "game authentication confirmed" not in failure_text, failure_text[-6000:]
            assert reason in host_log.read_text(errors="replace"), host_log.read_text(errors="replace")[-6000:]
            assert "AddressSanitizer" not in failure_text and "runtime error:" not in failure_text and "LeakSanitizer" not in failure_text, failure_text[-6000:]
            assert host.poll() is None, host_log.read_text(errors="replace")[-6000:]
        client = subprocess.run(client_command, stdout=client_output, stderr=subprocess.STDOUT, timeout=40)
        assert client.returncode == 0, client_log.read_text(errors="replace")[-6000:]
        assert host.poll() is None, host_log.read_text(errors="replace")[-6000:]
        host.send_signal(signal.SIGTERM)
        assert host.wait(timeout=15) == 0, host_log.read_text(errors="replace")[-6000:]
    finally:
        if host.poll() is None:
            host.kill()
            host.wait(timeout=5)
host_text = host_log.read_text(errors="replace")
client_text = client_log.read_text(errors="replace")
assert "OnClientEnterWorld" in host_text and "Native engine peer entered:" in client_text and "game authentication confirmed" in client_text
for text in (host_text,client_text):
    assert "AddressSanitizer" not in text and "runtime error:" not in text and "LeakSanitizer" not in text, text[-6000:]
print(client_text.strip())
print("Original server rejected incorrect weapon and retail client checksums, then accepted a valid peer")
print("Original native host and separate native engine peer completed world/preload negotiation, player assignment, original game authentication, two seconds of updates and clean shutdown")
