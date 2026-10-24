#!/usr/bin/env python3
"""Build pinned native asset-import tools into out/deps; no game data downloads."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / "out/deps"


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def checkout(name, repository, revision):
    target = DEPS / name
    if not (target / ".git").exists():
        run("git", "clone", "--no-checkout", repository, target)
    run("git", "-C", target, "checkout", "--detach", revision)
    return target


def asset_tools(jobs):
    srep = checkout("srep", "https://github.com/Intensity/srep.git", "48f4b97a1c6db91b4d2dcc0b96539d639e5fb899")
    run("make", "-C", srep, f"-j{jobs}", "STATIC=", "DEBUGGING=", "VERBOSITY=")
    unarc = checkout("unarc", "https://github.com/xredor/unarc.git", "adc333d6cdd76d72da254cc80d766fbbcc683c95")
    # The original extractor lacks the SREP stage used by the supplied image.
    external = unarc / "Compression/External/C_External.cpp"
    original = subprocess.check_output(["git", "-C", str(unarc), "show", "HEAD:Compression/External/C_External.cpp"])
    command = shlex.quote(str(srep / "bin/srep")) + " -d packed.bin data7777"
    config = "[External compressor:srep]\npackedfile=packed.bin\ndatafile=data7777\nunpackcmd=" + command
    # The legacy-format branch assigned a temporary owning MYFILE by value,
    # leaving dangling filenames. Update the name without copying ownership.
    original = original.replace(b'outfile = "data7777";', b'outfile.setname("data7777");')
    external.write_bytes(original + ("\nstatic int NOLF_SREP = AddExternalCompressor(" + json.dumps(config) + ");\n").encode())
    common = unarc / "Compression/Common.cpp"
    original = subprocess.check_output(["git", "-C", str(unarc), "show", "HEAD:Compression/Common.cpp"])
    # Native absolute tool paths must not be prefixed with './'.
    common.write_bytes(original.replace(b'sprintf(cmd, "./%s%s",', b'sprintf(cmd, "%s%s",'))
    # Reject unsupported codecs before creating worker threads. Upstream can
    # otherwise deadlock while joining a producer after a consumer fails.
    library = unarc / "Compression/CompressionLibrary.cpp"
    original = subprocess.check_output(["git", "-C", str(unarc), "show", "HEAD:Compression/CompressionLibrary.cpp"])
    needle = b"  // Start N threads\n"
    replacement = b"""  for (int i=0; i<N; ++i) {
    COMPRESSION_METHOD* codec = ParseCompressionMethod(cm[i]);
    if (!codec) { free(method); return FREEARC_ERRCODE_INVALID_COMPRESSOR; }
    delete codec;
  }
  // Start N threads
"""
    if original.count(needle) != 1:
        raise RuntimeError("unarc source differs from the pinned patch context")
    library.write_bytes(original.replace(needle, replacement))
    run("make", "-C", unarc, f"-j{jobs}", "DEBUG_FLAGS=-g", "UNIX_LINK_FLAGS=-pthread")
    print(f"Asset importer ready: --unarc {unarc / 'unarc'}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-tools", action="store_true", required=True)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    asset_tools(max(1, args.jobs))
