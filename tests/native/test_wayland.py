#!/usr/bin/env python3
"""Run an SDL Wayland test in a private headless Weston compositor session."""
from pathlib import Path
import os
import signal
import subprocess
import sys
import tempfile
import time


def stop(process):
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def tail(log):
    log.flush()
    log.seek(0, os.SEEK_END)
    length = log.tell()
    log.seek(max(0, length - 65536))
    return log.read().decode(errors='replace')


def main():
    if len(sys.argv) < 3:
        raise RuntimeError('expected Weston and native test executable')
    timeout = int(os.environ.get('NOLF_TEST_TIMEOUT', '30'))
    if not 1 <= timeout <= 600:
        raise ValueError('NOLF_TEST_TIMEOUT must be between 1 and 600 seconds')
    with tempfile.TemporaryDirectory(prefix='nolf-wayland-') as temporary:
        root = Path(temporary)
        environment = os.environ.copy()
        for name in ('WAYLAND_DISPLAY', 'DISPLAY', 'SDL_VIDEODRIVER'):
            environment.pop(name, None)
        environment['XDG_RUNTIME_DIR'] = str(root)
        # These headless fixtures use Mesa software rendering on both display
        # backends. Avoid probing unrelated vendor drivers without a DRM device.
        environment['LIBGL_ALWAYS_SOFTWARE'] = '1'
        environment['GALLIUM_DRIVER'] = 'llvmpipe'
        vendor_files = sorted(Path('/usr/share/glvnd/egl_vendor.d').glob('*mesa*.json'))
        if vendor_files:
            environment['__EGL_VENDOR_LIBRARY_FILENAMES'] = ':'.join(str(path) for path in vendor_files)
        for kind in ('CONFIG', 'DATA', 'CACHE', 'STATE'):
            environment[f'XDG_{kind}_HOME'] = str(root / kind.lower())
        arguments = [sys.argv[1], '--backend=headless-backend.so', '--shell=kiosk-shell.so',
                     '--use-pixman', '--no-config', '--idle-time=0', '--socket=nolf-test', '--width=640', '--height=480']
        with tempfile.TemporaryFile() as log:
            compositor = subprocess.Popen(arguments, env=environment, stdin=subprocess.DEVNULL,
                                          stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 10
                while not (root / 'nolf-test').exists():
                    if compositor.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError('headless Weston did not become ready')
                    time.sleep(.01)
                environment['WAYLAND_DISPLAY'] = 'nolf-test'
                environment['SDL_VIDEODRIVER'] = 'wayland'
                environment['NOLF_EXPECT_VIDEO_DRIVER'] = 'wayland'
                try:
                    result = subprocess.run(sys.argv[2:], env=environment, stdin=subprocess.DEVNULL,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout)
                except subprocess.TimeoutExpired as error:
                    raise RuntimeError('native Wayland test timed out') from error
                if result.returncode or 'desktop=wayland' not in result.stdout:
                    raise RuntimeError(f'native Wayland test failed ({result.returncode}):\n{result.stdout}')
                stop(compositor)
                if compositor.returncode:
                    raise RuntimeError(f'headless Weston shutdown failed ({compositor.returncode})')
                print(result.stdout, end='')
            except BaseException:
                stop(compositor)
                print(tail(log), file=sys.stderr)
                raise
    return 0


if __name__ == '__main__':
    sys.exit(main())
