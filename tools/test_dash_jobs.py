#!/usr/bin/env python3
"""Run the shared D4 job/trap cases through a controlling Linux PTY."""

import os
from pathlib import Path
import pty
import resource
import select
import sys
import time

from dash_job_cases import frame_command, run_job_cases
from dash_wait_cases import run_wait_cases
from dash_pipeline_cases import drive_terminal, run_pipeline_cases
from wait_observer import linux_suspend
from linux_session import terminate_session
from dash_resource_cases import run_resource_cases


def main(resource_control=None, resource_library=None):
    shell = str(Path(sys.argv[1]).resolve())
    pid, descriptor = pty.fork()
    if pid == 0:
        os.environ.update(PS1="JT> ", ENV="", LC_ALL="C",
                          PATH=str(Path(shell).parent) + os.pathsep + os.environ["PATH"])
        if resource_control:
            os.environ.update(LD_PRELOAD=str(resource_library), D4_FORK_CONTROL=str(resource_control))
            resource.setrlimit(resource.RLIMIT_NOFILE, (32, 32))
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

    def command(text, interrupt_pid=None, ready=None, steps=(), rejected=False, reject_after=None):
        nonlocal sequence
        sequence += 1
        if resource_control:
            budget = reject_after if rejected else -1
            resource_control.write_text(f"{sequence} {budget}\n")
        done = f"JT_DONE_{sequence}"
        text = frame_command(text, done)
        start = len(output)
        os.write(descriptor, text.encode() + b"\n")
        begin = expect(text.encode() + b"\n", start)
        if interrupt_pid is not None:
            assert interrupt_pid == pid
            print(linux_suspend(pid), flush=True)
            os.write(descriptor, b"\x03")
        drive_terminal(expect, lambda data: os.write(descriptor, data), begin, steps)
        if rejected:
            end = expect(b"Cannot fork\n", begin)
            expect(b"JT> ", end)
            if resource_control and b"RX_INJECTED_FORK\n" not in output[begin:]:
                raise AssertionError(f"reference failed without the requested fork injection: {output[begin:]!r}")
            return output[begin:]
        end = expect(done.encode() + b"\n", begin)
        expect(b"JT> ", end)
        if ready is not None:
            expect(ready, begin)
        return output[begin:]

    def record(name, passed, detail=""):
        checks.append(passed)
        print(f"[{'PASS' if passed else 'FAIL'}] {name} {detail}", flush=True)

    try:
        expect(b"JT> ", 0)
        if resource_control:
            run_resource_cases(command, record, lambda: len(list(Path(f'/proc/{pid}/fd').iterdir())),
                               suspended=False)
        else:
            run_job_cases(command, record)
            run_wait_cases(command, record)
            run_pipeline_cases(command, record)
        print(f"Linux reference job/trap cases: {sum(checks)} passed, {checks.count(False)} failed")
    finally:
        try:
            if len(sys.argv) > 2:
                Path(sys.argv[2]).write_bytes(output)
        finally:
            terminated = terminate_session(pid)
            os.waitpid(pid, 0)
            os.close(descriptor)
            print(f"LINUX_SESSION_CLEANUP sid={pid} pids={terminated}", flush=True)


if __name__ == "__main__":
    main()
