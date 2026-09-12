#!/usr/bin/env python3
"""Run the shared D4 job/trap cases through a controlling Linux PTY."""

import os
from pathlib import Path
import pty
import select
import signal
import sys
import time

from dash_job_cases import frame_command, run_job_cases


def main():
    shell = str(Path(sys.argv[1]).resolve())
    pid, descriptor = pty.fork()
    if pid == 0:
        os.environ.update(PS1="JT> ", ENV="", LC_ALL="C",
                          PATH=str(Path(shell).parent) + os.pathsep + os.environ["PATH"])
        os.execv(shell, [shell, "-i"])
    output = b""
    sequence = 0
    checks = []

    def expect(marker, start):
        nonlocal output
        deadline = time.monotonic() + 15
        while marker not in output[start:]:
            if time.monotonic() >= deadline:
                raise TimeoutError(f"missing {marker!r}: {output[start:]!r}")
            if select.select([descriptor], [], [], 0.1)[0]:
                data = os.read(descriptor, 4096)
                if not data:
                    raise EOFError("reference shell exited")
                output += data.replace(b"\r", b"")
        return output.index(marker, start) + len(marker)

    def command(text):
        nonlocal sequence
        sequence += 1
        done = f"JT_DONE_{sequence}"
        text = frame_command(text, done)
        start = len(output)
        os.write(descriptor, text.encode() + b"\n")
        begin = expect(text.encode() + b"\n", start)
        end = expect(done.encode() + b"\n", begin)
        expect(b"JT> ", end)
        return output[begin:end]

    def record(name, passed, detail=""):
        checks.append(passed)
        print(f"[{'PASS' if passed else 'FAIL'}] {name} {detail}", flush=True)

    try:
        expect(b"JT> ", 0)
        run_job_cases(command, record)
        print(f"Linux reference job/trap cases: {sum(checks)} passed, {checks.count(False)} failed")
    finally:
        if len(sys.argv) > 2:
            Path(sys.argv[2]).write_bytes(output)
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        os.close(descriptor)


if __name__ == "__main__":
    main()
