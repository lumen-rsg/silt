#!/usr/bin/env python3
"""Regression for deferred Ctrl-C in the pinned dash Linux reference build."""

import os
import pty
import select
import signal
import sys
import time


def main():
    pid, descriptor = pty.fork()
    if pid == 0:
        os.environ["PS1"] = "REFERENCE> "
        os.execv(sys.argv[1], [sys.argv[1], "-i"])

    def expect(marker):
        data = b""
        deadline = time.monotonic() + 5
        while marker not in data and time.monotonic() < deadline:
            if select.select([descriptor], [], [], 0.1)[0]:
                data += os.read(descriptor, 4096)
        if marker not in data:
            raise AssertionError(f"missing {marker!r}: {data!r}")

    try:
        expect(b"REFERENCE> ")
        for _ in range(3):
            # Ensure the reference shell has entered its blocking read.
            time.sleep(0.1)
            os.write(descriptor, b"\x03")
            expect(b"REFERENCE> ")
        os.write(descriptor, b"printf 'REFERENCE_ALIVE\\n'\n")
        expect(b"REFERENCE_ALIVE\r\n")
        print("dash Linux PTY: three deferred interrupts and recovery PASS")
    finally:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        os.close(descriptor)


if __name__ == "__main__":
    main()
