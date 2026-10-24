#!/usr/bin/env python3
"""Loopback protocol fixture; no external master or game assets are contacted."""
import selectors
import socket
import struct
import subprocess
import sys
import threading
import time


def main():
    with socket.socket() as master, socket.socket(type=socket.SOCK_DGRAM) as good, socket.socket(type=socket.SOCK_DGRAM) as incomplete:
        for sock in (master, good, incomplete):
            sock.bind(('127.0.0.1', 0))
            sock.settimeout(10)
        master.listen()
        failures = []

        def serve_master():
            try:
                with master.accept()[0] as client:
                    client.settimeout(8)
                    # Deliberately split both the key and the challenge across reads.
                    for chunk in (b'\\ba', b'sic\\\\secure\\AB', b'CD', b'EF'):
                        client.sendall(chunk)
                        time.sleep(.02)
                    request = b''
                    while b'\\list\\cmp\\gamename\\nolf\\final\\' not in request:
                        chunk = client.recv(13)
                        assert chunk, 'browser closed before its full request'
                        request += chunk
                        assert len(request) < 1024, 'unbounded browser request'
                    assert request.startswith(b'\\gamename\\nolf\\enctype\\0\\validate\\'), request
                    assert b'\\queryid\\1.1\\' in request
                    record = lambda sock: socket.inet_aton('127.0.0.1') + struct.pack('!H', sock.getsockname()[1])
                    data = record(good) * 2 + b'\0' * 6 + record(incomplete) + b'\\final\\'
                    for byte in data:
                        client.sendall(bytes((byte,)))
                        time.sleep(.001)
            except BaseException as error:
                failures.append(error)

        thread = threading.Thread(target=serve_master, daemon=True)
        thread.start()
        selector = selectors.DefaultSelector()
        for sock in (good, incomplete):
            sock.setblocking(False)
            selector.register(sock, selectors.EVENT_READ)
        pending = []
        with subprocess.Popen([sys.argv[1], f'127.0.0.1:{master.getsockname()[1]}'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as process:
            deadline = time.monotonic() + 12
            while process.poll() is None and time.monotonic() < deadline:
                now = time.monotonic()
                for when, sock, peer in pending[:]:
                    if when <= now:
                        sock.sendto(b'\\hostname\\Native NOLF\\player_0\\Cate Archer\\queryid\\17.1', peer)
                        pending.remove((when, sock, peer))
                for key, _ in selector.select(.01):
                    data, peer = key.fileobj.recvfrom(2048)
                    assert data == b'\\status\\', data
                    final = b'\\numplayers\\1\\maxplayers\\8\\gametype\\Deathmatch\\mapname\\DM01\\final\\\\queryid\\17.2'
                    key.fileobj.sendto(final, peer)
                    key.fileobj.sendto(final, peer)
                    if key.fileobj is good:
                        # A malformed identity must not overflow or complete a reply.
                        key.fileobj.sendto(b'\\hostname\\bad\\queryid\\17.99999999999999999999999', peer)
                        pending.append((now + .15, good, peer))
            if process.poll() is None:
                process.kill()
            output, error = process.communicate(timeout=5)
            thread.join(timeout=2)
            if failures:
                raise failures[0]
            assert process.returncode == 0, output + error
            print(output, end='')
        selector.close()


if __name__ == '__main__':
    main()
