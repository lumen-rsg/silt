#!/usr/bin/env python3
"""Force the pending-check/read SIGINT race in the prepared Linux Dash."""

import os
from pathlib import Path
import pty
import select
import subprocess
import sys
import tempfile
import time

from linux_session import terminate_session


def main():
    with tempfile.TemporaryDirectory(prefix="d4-read-signal-") as directory:
        control = Path(directory) / "control"
        control.write_bytes(b"\0")
        library = Path(directory) / "read-signal.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(Path(__file__).with_name("dash_read_signal.c")), "-ldl",
                        "-o", str(library)], check=True)
        pid, descriptor = pty.fork()
        if pid == 0:
            os.environ.update(PS1="READ_SIGNAL> ", LD_PRELOAD=str(library),
                              D4_READ_SIGNAL_CONTROL=str(control))
            os.execv(sys.argv[1], [sys.argv[1], "-i"])
        transcript = bytearray()

        def expect(marker, start):
            deadline = time.monotonic() + 5
            while marker not in transcript[start:]:
                if time.monotonic() >= deadline:
                    raise AssertionError(f"missing {marker!r}: {bytes(transcript[start:])!r}")
                if select.select([descriptor], [], [], 0.1)[0]:
                    transcript.extend(os.read(descriptor, 4096).replace(b"\r", b""))
            return transcript.index(marker, start) + len(marker)

        try:
            expect(b"READ_SIGNAL> ", 0)
            for generation in range(1, 17):
                start = len(transcript)
                control.write_bytes(bytes([generation]))
                os.write(descriptor, b"\n")
                after = expect(b"D4_READ_SIGNAL_INJECTED\n", start)
                expect(b"READ_SIGNAL> ", after)
                start = len(transcript)
                os.write(descriptor, b"printf 'READ_SIGNAL_ALIVE\\n'\n")
                after = expect(b"READ_SIGNAL_ALIVE\n", start)
                expect(b"READ_SIGNAL> ", after)
            print("Linux pending-check/read: 16 injected SIGINTs and recoveries PASS")
        finally:
            try:
                if len(sys.argv) > 2:
                    Path(sys.argv[2]).write_bytes(transcript)
            finally:
                terminated = terminate_session(pid)
                os.waitpid(pid, 0)
                os.close(descriptor)
                print(f"LINUX_SESSION_CLEANUP sid={pid} pids={terminated}")


if __name__ == "__main__":
    main()
