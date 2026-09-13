#!/usr/bin/env python3
"""Boot Neva with the Silt rootfs and exercise the first userland surface."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import socket
import re
import shutil

from dash_job_cases import frame_command, run_job_cases
from dash_wait_cases import run_wait_cases
from dash_pipeline_cases import drive_terminal, run_pipeline_cases
from dash_resource_cases import run_resource_cases
from wait_observer import guest_suspend


def command_output(
    session, runner, command: str, markers: tuple[bytes, ...], timeout: int = 30,
) -> bytes:
    command_bytes = command.encode()
    if len(command_bytes) > 126 or b"\n" in command_bytes:
        raise ValueError(f"command exceeds nsh's single-line input contract: {command!r}")
    start = len(session.output)
    session.send(command)
    if not session.read_until(command_bytes, timeout=timeout, start_offset=start):
        raise TimeoutError(f"complete command was not echoed: {command!r}")
    marker_offset = session.output.index(command_bytes, start) + len(command_bytes)
    if not session.read_until(runner.PROMPT, timeout=timeout, start_offset=marker_offset):
        tail = session.output[start:].decode(errors="replace")[-2000:]
        raise TimeoutError(
            f"prompt did not return after {command!r}; output={tail!r}"
        )
    # nsh writes its prompt directly while Silt applications write through
    # ttyd, so the prompt can overtake the final service-backed output bytes.
    # Wait for declared output after the prompt as well as before it.
    for marker in markers:
        if marker not in session.output[marker_offset:] and not session.read_until(
            marker, timeout=timeout, start_offset=marker_offset,
        ):
            tail = session.output[marker_offset:].decode(errors="replace")[-2000:]
            raise TimeoutError(
                f"missing output {marker!r} after {command!r}; output={tail!r}"
            )
    output = session.output[start:].replace(b"\r", b"")
    output = output.replace(runner.PROMPT, b"")
    lines = output.split(b"\n")
    for index, line in enumerate(lines):
        if line.strip() == command_bytes:
            del lines[index]
            break
    if lines and lines[-1].strip() == runner.PROMPT.strip():
        lines.pop()
    return b"\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--neva-source", type=Path, required=True)
    parser.add_argument("--neva-build", type=Path, required=True)
    parser.add_argument("--kernel", type=Path,
                        help="alternative kernel ELF; --gdb-log requires matching neva-build ttyd symbols")
    parser.add_argument("--rootfs", type=Path, required=True)
    parser.add_argument("--smp", type=int, default=4)
    parser.add_argument("--restart-tests", action="store_true",
                        help="inject idle ttyd/sessiond EL0 crashes through local QEMU GDB")
    parser.add_argument("--uart-log", type=Path, help="retain complete guest UART output")
    parser.add_argument("--gdb-log", type=Path, help="read-only guest snapshot on timeout")
    parser.add_argument("--terminal-cycles", type=int, default=1,
                        help="repeat the background/foreground termios sequence (1-128)")
    parser.add_argument("--prompt-interrupts", type=int, default=160,
                        help="repeat Ctrl-C at the interactive prompt (1-10000)")
    args = parser.parse_args()
    if not 1 <= args.terminal_cycles <= 128:
        parser.error("--terminal-cycles must be between 1 and 128")
    if not 1 <= args.prompt_interrupts <= 10000:
        parser.error("--prompt-interrupts must be between 1 and 10000")

    if shutil.which("gdb") is None:
        parser.error("the wait/trap gate requires GDB with AArch64 and Python support")

    neva_source = args.neva_source.resolve()
    neva_build = args.neva_build.resolve()
    rootfs = args.rootfs.resolve()
    kernel = args.kernel.resolve() if args.kernel else neva_build / 'neva.elf'
    if not rootfs.is_file():
        parser.error(f"rootfs image does not exist: {rootfs}")
    if not kernel.is_file():
        parser.error(f"Neva kernel does not exist: {kernel}")

    tools_dir = neva_source / "tools"
    sys.path.insert(0, str(tools_dir))
    sys.argv = [
        str(tools_dir / "test_runner.py"),
        str(neva_build),
        "--skip-build",
        f"--smp={args.smp}",
        f"--nevfs-image={rootfs}",
    ]
    import test_runner as runner
    runner.KERNEL_NAME = str(kernel)

    if args.gdb_log:
        # Snapshot symbols must match the booted bytes, even if a later build
        # replaces the build directory while a long stress run is active.
        debug_kernel = args.gdb_log.with_suffix(".elf").resolve()
        debug_tty = args.gdb_log.with_suffix(".ttyd.elf").resolve()
        shutil.copyfile(kernel, debug_kernel)
        shutil.copyfile(neva_build / 'apps/ttyd.elf', debug_tty)
        runner.KERNEL_NAME = str(debug_kernel)

    # The wait/trap gate observes signal suspension through read-only GDB.
    from qemu_service_fault import crash_idle_service
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        debug_port = listener.getsockname()[1]
    original_command = runner.qemu_command
    runner.qemu_command = lambda media: original_command(media) + [
        '-gdb', f'tcp:127.0.0.1:{debug_port}',
    ]

    session = runner.QemuSession()
    checks: list[tuple[str, bool, str]] = []

    def record(name: str, passed: bool, detail: str = "") -> None:
        checks.append((name, passed, detail))
        suffix = f": {detail}" if detail else ""
        print(f"  [{'PASS' if passed else 'FAIL'}] {name}{suffix}")

    def run(command: str, markers: tuple[bytes, ...], name: str, timeout: int = 30,
            stop_on_timeout: bool = True) -> None:
        try:
            output = command_output(session, runner, command, markers, timeout)
            lines = tuple(line.strip() for line in output.splitlines())

            def marker_present(marker: bytes) -> bool:
                if marker.startswith(b"status="):
                    return marker in lines
                return marker in output

            missing = [marker.decode(errors="replace")
                       for marker in markers if not marker_present(marker)]
            unknown = b"Unknown: " in output or b"File not found." in output
            passed = not missing and not unknown
            detail = ""
            if not passed:
                reason = "missing " + ", ".join(missing) if missing \
                    else "shell rejected command"
                rendered = output.decode(errors="replace").strip().replace(
                    "\n", " | ",
                )
                detail = reason + "; output=" + rendered[-600:]
            record(name, passed, detail)
        except TimeoutError as error:
            record(name, False, str(error))
            if stop_on_timeout:
                raise

    try:
        print("=== Booting Neva with the Silt R0 rootfs ===")
        session.start()
        if not session.read_until(runner.PROMPT, timeout=120):
            print(session.output.decode(errors="replace")[-10000:])
            return 1
        boot_markers = (
            b"B2_INITD_READY: PASS",
            b"B3_INITD_READY: PASS",
            b"B5_EL0_PROVIDERS: fat32/ext4/nevfs PASS",
            b"B5_VFSD: namespace/data PASS",
            b"B8_SESSION_READY: uid-scoped manager/catalog PASS",
        )
        session.read_until(boot_markers[1], timeout=120)
        for marker in boot_markers:
            record("boot:" + marker.decode().split(":", 1)[0],
                   marker in session.output)
        failures = [line for line in session.output.decode(errors="replace").splitlines()
                    if re.search(r": FAIL(?:\s|$)", line)]
        record("boot self-tests contain no failure", not failures, " | ".join(failures[-8:]))
        if failures or any(marker not in session.output for marker in boot_markers):
            return 1

        # The recovery prompt can appear before the asynchronous kernel/service
        # self-tests finish. Re-synchronize after B3_INITD_READY so later
        # commands cannot be swallowed by their terminal restart coverage.
        start = len(session.output)
        session.send("")
        if not session.read_until(runner.PROMPT, timeout=30, start_offset=start):
            print(session.output.decode(errors="replace")[-10000:])
            return 1

        print("\n=== Interactive recovery shell and Silt commands ===")
        start = len(session.output)
        session.send_raw(b"abouX\x7ft\n")
        edited = session.read_until(
            runner.PROMPT, timeout=15, start_offset=start,
        ) and b"nsh: neva shell v1.0" in session.output[start:]
        record("nsh canonical input editing", edited)

        run("kbdstat", (b"irq=", b"keys=", b"devices="), "input service")
        run("cat /etc/os-release", (b"NAME=Silt", b"ID=silt"),
            "Silt release metadata")
        run("cat /etc/motd", (b"Silt OS userspace on the Neva microkernel",),
            "Silt motd")
        run("sysinfo", (b"OS: Silt 0.1.0", b"Kernel: Neva 0.1.0",
                        b"Architecture: aarch64", b"Identity: uid=1000"),
            "sysinfo")
        run("meminfo", (b"Kernel heap:", b"Physical pages:"), "meminfo")
        run("ps", (b"PID  PPID UID  STATE", b"1000"), "ps")
        run("uname -a", (b"Silt neva 0.1.0 aarch64",), "uname")
        run("id", (b"uid=1000(session)", b"gid=1000", b"euid=1000"), "id")
        run("whoami", (b"session",), "whoami")
        run("echo SILT_E2E_PAYLOAD", (b"SILT_E2E_PAYLOAD",), "echo")
        run("true", (), "true launch")
        run("status", (b"status=0",), "true exit status")
        run("false", (), "false launch")
        run("status", (b"status=1",), "false exit status")

        print("\n=== dash built-in language suite ===")
        run("dash -c ':'", (), "dash null command")
        run("status", (b"status=0",), "dash null status")
        run("dash -c 'true'", (), "dash true")
        run("status", (b"status=0",), "dash true status")
        run("dash -c 'false'", (), "dash false")
        run("status", (b"status=1",), "dash false status")
        run(
            "dash -c 'value=41;value=$((value+1));printf \"DASH_ARITH=%s\\n\" \"$value\"'",
            (b"DASH_ARITH=42",),
            "dash assignment and arithmetic",
        )
        run("status", (b"status=0",), "dash arithmetic status")
        run(
            "dash -c 'word=\"silt shell\";printf \"DASH_QUOTE=<%s>\\n\" \"$word\"'",
            (b"DASH_QUOTE=<silt shell>",),
            "dash quoting and parameter expansion",
        )
        run("status", (b"status=0",), "dash expansion status")
        run(
            "dash -c 'if true;then printf \"DASH_FLOW=ok\\n\";else false;fi'",
            (b"DASH_FLOW=ok",),
            "dash control flow",
        )
        run("status", (b"status=0",), "dash control-flow status")

        print("\n=== dash descriptor and filesystem suite ===")
        run(
            "dash /boot/dash-d2.sh",
            (b"DASH_SCRIPT=ok",),
            "dash script input and stat",
        )
        run("status", (b"status=0",), "dash script status")
        run(
            "dash -c 'printf \"discarded\\n\" >/dev/null;printf \"DASH_REDIR=ok\\n\"'",
            (b"DASH_REDIR=ok",),
            "dash /dev/null redirection",
        )
        run("status", (b"status=0",), "dash device redirection status")
        run(
            "dash -c 'printf \"DASH_TMP=ok\\n\" >/tmp/dash.out;read value </tmp/dash.out;printf \"%s\\n\" \"$value\"'",
            (b"DASH_TMP=ok",),
            "dash private tmp redirection",
        )
        run("status", (b"status=0",), "dash file redirection status")
        run(
            "dash -c 'cd /etc && test -f os-release && pwd'",
            (b"/etc",),
            "dash cd, getcwd, and test",
        )
        run("status", (b"status=0",), "dash cwd status")
        run(
            "dash -c 'for path in /etc/os-*;do printf \"DASH_GLOB=%s\\n\" \"$path\";done'",
            (b"DASH_GLOB=/etc/os-release",),
            "dash directory enumeration",
        )
        run("status", (b"status=0",), "dash glob status")
        run(
            "dash -c 'printf \"DASH_TTY=ok\\n\" >/dev/tty'",
            (b"DASH_TTY=ok",),
            "dash explicit tty redirection",
        )
        run("status", (b"status=0",), "dash tty redirection status")
        run(
            "dash -c 'if test -r /etc/shadow;then false;else printf \"DASH_SHADOW=denied\\n\";fi'",
            (b"DASH_SHADOW=denied",),
            "dash directory grant data ceiling",
        )
        run("status", (b"status=0",), "dash shadow denial status")

        print("\n=== dash process execution suite ===")
        run(
            "dash -c 'uname -a'",
            (b"Silt neva 0.1.0 aarch64",),
            "dash external command",
        )
        run("status", (b"status=0",), "dash external command status")
        run(
            "dash -c '(printf \"DASH_SUBSHELL=ok\\n\")'",
            (b"DASH_SUBSHELL=ok",),
            "dash forked subshell",
        )
        run("status", (b"status=0",), "dash subshell status")
        run(
            "dash -c 'uname >/dev/null && printf \"DASH_AND=ok\\n\"'",
            (b"DASH_AND=ok",),
            "dash external command and conditional",
        )
        run("status", (b"status=0",), "dash conditional status")
        run(
            "dash -c 'missing-silt-command 2>/dev/null || printf \"DASH_OR=ok\\n\"'",
            (b"DASH_OR=ok",),
            "dash exec failure and fallback",
        )
        run("status", (b"status=0",), "dash exec failure status")

        print("\n=== dash pipeline suite ===")
        run(
            "dash -c 'printf \"DASH_PIPE=ok\\n\" | "
            "{ IFS= read -r line; printf \"%s\\n\" \"$line\"; }'",
            (b"DASH_PIPE=ok",),
            "dash builtin pipeline",
        )
        run("status", (b"status=0",), "dash builtin pipeline status")
        run(
            "dash -c 'printf \"DASH_MULTI=ok\\n\" | "
            "{ read x; printf \"%s\\n\" \"$x\"; } | "
            "{ read x; printf \"%s\\n\" \"$x\"; }'",
            (b"DASH_MULTI=ok",),
            "dash multi-stage pipeline",
        )
        run("status", (b"status=0",), "dash multi-stage pipeline status")
        run(
            "dash -c 'value=$(printf \"DASH_SUBSTITUTE=ok\"); "
            "printf \"%s\\n\" \"$value\"'",
            (b"DASH_SUBSTITUTE=ok",),
            "dash command substitution",
        )
        run("status", (b"status=0",), "dash command substitution status")
        run(
            "dash -c 'echo DASH_EXTERNAL_PIPE=ok | "
            "{ IFS= read -r line; printf \"%s\\n\" \"$line\"; }'",
            (b"DASH_EXTERNAL_PIPE=ok",),
            "dash external producer pipeline",
        )
        run("status", (b"status=0",), "dash external pipeline status")
        run(
            "dash -c 'echo DASH_EXTERNAL_REDIR=ok >/tmp/dash.out; "
            "IFS= read -r line </tmp/dash.out; printf \"%s\\n\" \"$line\"'",
            (b"DASH_EXTERNAL_REDIR=ok",),
            "dash external descriptor handoff",
        )
        run("status", (b"status=0",), "dash external redirection status")
        run(
            "dash -c 'DASH_ENV=ok; export DASH_ENV; dash -c "
            "\"printf \\\"DASH_ENV=%s\\\\n\\\" \\\"\\$DASH_ENV\\\"\"'",
            (b"DASH_ENV=ok",),
            "dash environment handoff",
        )
        run("status", (b"status=0",), "dash environment handoff status")

        print("\n=== dash interactive job control ===")
        run("check-terminal peek", (b"D4_EVENT_PEEK: observer/consumer/empty/flags/rights/stale PASS",),
            "non-consuming Event observation")
        run("status", (b"status=0",), "Event observation status")
        run("check-cleanup", (
            b"D4_CLEANUP: nonresident signal handler PASS",
            b"D4_CLEANUP: acquisition/nested guards/mask/CLOEXEC PASS",
            b"D4_CLEANUP: fork isolation/failed exec/exec close PASS",
            b"D4_CLEANUP: caught reply/default-fatal IPC ownership PASS",
            b"D4_CLEANUP: pipe/tty EINTR/longjmp capacity PASS",
            b"D4_CLEANUP: background write/attributes/foreground capacity PASS",
        ), "Silt interrupted-operation cleanup", timeout=180, stop_on_timeout=True)
        run("status", (b"status=0",), "Silt cleanup status")
        run("check-cleanup quota", (b"D4_RESOURCE: quota EAGAIN/reap/refill/handle capacity PASS",),
            "Silt process quota rollback", timeout=90)
        run("status", (b"status=0",), "Silt process quota status")
        run("check-faults", (b"D4_FAULTS: typed wait/default/ignore/block/catch/nested/escape PASS",),
            "Silt synchronous fault signal status")
        run("status", (b"status=0",), "Silt fault status checks")
        run("check-pager-smp", (
            b"D4_PAGER_SMP: concurrent cold RX faults PASS",
        ), "SMP pager request coalescing")
        run("status", (b"status=0",), "SMP pager status")
        run("check-pager-smp asid", (
            b"D4_ASID: long-lived owners across 272 fork lifetimes PASS",
        ), "SMP ASID lifetime isolation", timeout=180)
        run("status", (b"status=0",), "SMP ASID status")
        run("check-pipes", (
            b"D4_PIPES: kill/fault/raw-exit drain PASS",
            b"D4_PIPES: blocked EOF/EPIPE PASS",
            b"D4_PIPES: reader/writer broadcast PASS",
            b"D4_PIPES: dup/fork/exec/CLOEXEC PASS",
            b"D4_PIPES: SIGPIPE/rights/revoke PASS",
            b"D4_PIPES: atomic writes/exhaustion/suspended death PASS",
            b"D4_PIPES: repeated forced teardown PASS",
        ), "Silt forced-death pipe lifecycle")
        run("status", (b"status=0",), "Silt pipe lifecycle status")
        run("check-terminal", (b"D4_TERMINAL: stop/resume/ignore/block/catch/authority PASS",),
            "Silt background terminal policy")
        run("status", (b"status=0",), "Silt terminal policy status")
        run("check-signals", (b"D4_SIGNALS: query/mask/suspend/fork/termios PASS",),
            "Silt signal and terminal ABI")
        start = len(session.output)
        session.send("dash -i")
        if not session.read_until(b"$ ", timeout=15, start_offset=start):
            raise TimeoutError("dash interactive prompt missing")
        session.send("PS1='D4> '")
        session.send("printf 'D4_READY\\n'")
        if not session.read_until(b"D4_READY\n", timeout=15, start_offset=start):
            raise TimeoutError(f"dash interactive startup failed: {session.output[start:]!r}")
        ready_end = session.output.find(b"D4_READY\n", start) + len(b"D4_READY\n")
        session.read_until(b"D4> ", timeout=15, start_offset=ready_end)

        command_sequence = 0
        def interactive(command: str, marker: bytes = b"D4> ", interrupt_pid=None,
                        ready=None, steps=(), rejected=False, reject_after=None) -> bytes:
            nonlocal command_sequence
            command_sequence += 1
            begin = len(session.output)
            done = f"D4_DONE_{command_sequence}".encode()
            command = frame_command(command, done.decode())
            session.send(command)
            echoed = command.encode()
            if not session.read_until(echoed, timeout=15, start_offset=begin):
                raise TimeoutError(f"missing interactive echo: {command}")
            after_echo = session.output.find(echoed, begin) + len(echoed)
            def expect_step(expected, start):
                if not session.read_until(expected, timeout=15, start_offset=start):
                    raise TimeoutError(f"missing pipeline acknowledgement {expected!r}: {session.output[start:]!r}")
                return session.output.find(expected, start) + len(expected)

            drive_terminal(expect_step, session.send_raw, after_echo, steps)
            if interrupt_pid is not None:
                print(guest_suspend(debug_kernel if args.gdb_log else kernel,
                                    debug_port, interrupt_pid), flush=True)
                session.send_raw(b"\x03")
            if rejected:
                if not session.read_until(b"Cannot fork\n", timeout=15, start_offset=after_echo):
                    raise TimeoutError(f"missing fork rejection: {session.output[after_echo:]!r}")
                failure_end = session.output.find(b"Cannot fork\n", after_echo) + len(b"Cannot fork\n")
                if not session.read_until(marker, timeout=15, start_offset=failure_end):
                    raise TimeoutError(f"missing rejection prompt: {session.output[after_echo:]!r}")
                return session.output[after_echo:]
            if not session.read_until(done + b"\n", timeout=15, start_offset=after_echo):
                raise TimeoutError(f"missing interactive output: {command}; {session.output[begin:]!r}")
            done_end = session.output.find(done + b"\n", after_echo) + len(done) + 1
            if not session.read_until(marker, timeout=15, start_offset=done_end):
                raise TimeoutError(f"missing interactive prompt after: {command}")
            if ready is not None and not session.read_until(ready, timeout=15, start_offset=after_echo):
                raise TimeoutError(f"missing child readiness after: {command}")
            return session.output[after_echo:]

        run_job_cases(interactive, record)
        run_wait_cases(interactive, record)
        run_pipeline_cases(interactive, record)
        def inherited_descriptors():
            output = interactive('check-cleanup fds')
            matches = re.findall(rb'(?:^|\n)RX_FDS=(\d+)\n', output)
            if len(matches) != 1:
                raise AssertionError(f"missing inherited descriptor count: {output!r}")
            return int(matches[0])

        run_resource_cases(interactive, record, inherited_descriptors, suspended=True)

        output = interactive("echo D4_EXTERNAL")
        record("dash interactive external command", b"D4_EXTERNAL" in output)
        for kind, status, diagnostic in (
            ("segv", 139, b"Segmentation fault"),
            ("ill", 132, b"Illegal instruction"),
            ("bus", 135, b"Bus error"),
        ):
            output = interactive(f"check-faults {kind}; printf 'D4_FAULT_STATUS=%s\\n' \"$?\"")
            record(f"dash {kind} foreground status and diagnostic",
                   f"D4_FAULT_STATUS={status}\n".encode() in output and diagnostic in output)
        output = interactive("check-faults exit139; printf 'D4_NORMAL_STATUS=%s\\n' \"$?\"")
        record("dash normal exit 139 is not a signal diagnostic",
               b"D4_NORMAL_STATUS=139\n" in output and b"Segmentation fault" not in output)
        output = interactive("check-faults ill & p=$!; wait $p; printf 'D4_WAIT_STATUS=%s\\n' \"$?\"")
        record("dash background fault wait status", b"D4_WAIT_STATUS=132\n" in output)
        output = interactive("true | check-faults segv; printf 'D4_PIPE_FAULT=%s\\n' \"$?\"")
        record("dash last pipeline member fault status", b"D4_PIPE_FAULT=139\n" in output)
        output = interactive("check-faults segv | true; printf 'D4_PIPE_LAST=%s\\n' \"$?\"")
        record("dash successful last member overrides earlier fault", b"D4_PIPE_LAST=0\n" in output)
        output = interactive("(trap 'printf \"D4_EXIT_TRAP=%s\\n\" \"$?\"' EXIT; check-faults bus; exit $?)")
        record("dash EXIT trap observes fault status", b"D4_EXIT_TRAP=135\n" in output)
        start = len(session.output)
        session.send("(trap 'printf \"D4_CONT\\n\"' CONT; printf 'D4_RUNNING\\n'; while :; do :; done)")
        if not session.read_until(b"D4_RUNNING\n", timeout=15, start_offset=start):
            raise TimeoutError(f"job did not start: {session.output[start:]!r}")
        start = len(session.output)
        session.send_raw(b"\x1a")
        stopped_prompt = session.read_until(b"D4> ", timeout=15, start_offset=start)
        record("dash Ctrl-Z restores prompt", stopped_prompt,
               "" if stopped_prompt else repr(session.output[start:]))
        output = interactive("jobs")
        record("dash jobs reports stopped job", b"Stopped" in output)
        start = len(session.output)
        output = interactive("bg")
        record("dash bg resumes job", b"while" in output)
        if not session.read_until(b"D4_CONT\n", timeout=15, start_offset=start):
            raise TimeoutError("background job did not acknowledge continuation")
        start = len(session.output)
        session.send("fg")
        # fg prints the command before changing the terminal owner. Wait for
        # the job's caught SIGCONT, not that early command display.
        if not session.read_until(b"D4_CONT\n", timeout=15, start_offset=start):
            raise TimeoutError("foreground job did not acknowledge continuation")
        start = len(session.output)
        session.send_raw(b"\x03")
        record("dash Ctrl-C interrupts foreground job", session.read_until(b"D4> ", timeout=15, start_offset=start))
        output = interactive("printf 'D4_ALIVE\\n'")
        record("dash survives foreground interrupt", b"D4_ALIVE" in output)
        for attempt in range(2):
            start = len(session.output)
            session.send_raw(b"\x03")
            record(f"dash prompt interrupt {attempt + 1}", session.read_until(b"D4> ", timeout=15, start_offset=start))
        for attempt in range(args.prompt_interrupts):
            start = len(session.output)
            session.send_raw(b"\x03")
            if not session.read_until(b"D4> ", timeout=15, start_offset=start):
                raise TimeoutError(f"dash repeated prompt interrupt {attempt + 1} failed")
        output = interactive("printf 'D4_INTERRUPT_STRESS_ALIVE\\n'")
        record(f"dash survives {args.prompt_interrupts} repeated prompt interrupts",
               b"D4_INTERRUPT_STRESS_ALIVE\n" in output)
        output = interactive("read value &")
        for attempt in range(5):
            output = interactive("jobs")
            if b"Stopped" in output: break
        record("dash background read SIGTTIN", b"Stopped" in output)
        start = len(session.output)
        session.send("fg")
        session.read_until(b"read value", timeout=10, start_offset=start)
        start = len(session.output)
        session.send("D4_INPUT")
        record("dash fg resumes terminal input", session.read_until(b"D4> ", timeout=15, start_offset=start))
        interactive("check-signals tostop")
        interactive("echo D4_BACKGROUND_WRITE &")
        for attempt in range(5):
            output = interactive("jobs")
            if b"Stopped" in output: break
        record("dash background write SIGTTOU", b"Stopped" in output)
        output = interactive("fg")
        record("dash foreground write resumes", b"D4_BACKGROUND_WRITE" in output.splitlines())
        for cycle in range(args.terminal_cycles):
            print(f"  terminal cycle {cycle + 1}/{args.terminal_cycles}")
            interactive("check-signals normal")
            interactive("check-terminal set-erase &")
            for attempt in range(5):
                output = interactive("jobs")
                if b"Stopped" in output: break
            record("dash background termios SIGTTOU without TOSTOP", b"Stopped" in output)
            output = interactive("check-terminal show-erase")
            record("stopped setter has not mutated termios", b"D4_ERASE=127" in output)
            interactive("bg")
            for attempt in range(5):
                output = interactive("jobs")
                if b"Stopped" in output: break
            record("background continuation stops setter again", b"Stopped" in output)
            output = interactive("fg")
            record("foreground continuation completes setter", b"D4_ERASE_SET" in output)
            output = interactive("check-terminal show-erase")
            record("foreground setter mutation visible", b"D4_ERASE=8" in output)
            interactive("check-terminal reset-erase")
        output = interactive("printf 'D4_PIPELINE\\n' | { read line; printf '%s\\n' \"$line\"; }")
        record("dash foreground pipeline", b"D4_PIPELINE" in output)
        if args.restart_tests:
            def restart_service(interface: int, marker: bytes) -> None:
                begin = len(session.output)
                diagnostic = crash_idle_service(debug_kernel if args.gdb_log else kernel,
                                                debug_port, interface)
                print(diagnostic.strip())
                if not session.read_until(marker, timeout=30, start_offset=begin):
                    raise TimeoutError(f"restart recovery missing {marker!r}: {session.output[begin:]!r}")
                print(f"D4_RECOVERED_SERVICE interface={interface}: {marker.decode()}")

            # Five live members exceed the old recovery enumeration capacity.
            interactive("(read pending) &")
            interactive("(read pending) &")
            for _ in range(8):
                output = interactive("jobs")
                if output.count(b"Stopped") >= 2: break
            record("restart background jobs stopped", output.count(b"Stopped") >= 2)
            begin = len(session.output)
            session.send("(printf 'D4_RESTART_READ_READY\\n'; read answer; printf 'D4_RESTART_READ:%s\\n' \"$answer\")")
            if not session.read_until(b"D4_RESTART_READ_READY\n", timeout=15, start_offset=begin):
                raise TimeoutError("foreground read job did not start")
            restart_service(0x4E45564154545931, b"C8_TTY_RESTART: foreground authority restored PASS")
            restart_service(0x4E45565543000000 + 1000,
                            b"C7_MANAGER_RESTART: retained generation/query-only recovery PASS")
            begin = len(session.output)
            session.send("D4_RECOVERED")
            read_ok = session.read_until(b"D4_RESTART_READ:D4_RECOVERED\n", timeout=15, start_offset=begin)
            prompt_ok = session.read_until(b"D4> ", timeout=15, start_offset=begin)
            record("foreground read survives ttyd and sessiond crashes", read_ok and prompt_ok,
                   "" if read_ok and prompt_ok else repr(session.output[begin:]))
            output = interactive("jobs")
            record("restart preserves stopped background jobs", output.count(b"Stopped") >= 2)
            for _ in range(2):
                begin = len(session.output)
                session.send("fg")
                session.read_until(b"read pending", timeout=10, start_offset=begin)
                session.send("done")
                if not session.read_until(b"D4> ", timeout=15, start_offset=begin):
                    raise TimeoutError("recovered background job did not finish")
            begin = len(session.output)
            session.send("(printf 'D4_RESTART_BUSY\\n'; while :; do :; done)")
            if not session.read_until(b"D4_RESTART_BUSY\n", timeout=15, start_offset=begin):
                raise TimeoutError("foreground busy job did not start")
            restart_service(0x4E45565543000000 + 1000,
                            b"C7_MANAGER_RESTART: retained generation/query-only recovery PASS")
            restart_service(0x4E45564154545931, b"C8_TTY_RESTART: foreground authority restored PASS")
            begin = len(session.output)
            session.send_raw(b"\x03")
            record("Ctrl-C targets recovered foreground job",
                   session.read_until(b"D4> ", timeout=15, start_offset=begin))
            output = interactive("printf 'D4_RESTART_ALIVE\\n'")
            record("dash alive after repeated service crashes", b"D4_RESTART_ALIVE" in output)
        start = len(session.output)
        session.send("exit")
        record("dash exit restores nsh", session.read_until(runner.PROMPT, timeout=15, start_offset=start))

        print("\n=== Session teardown ===")
        start = len(session.output)
        session.send("exit")
        teardown = session.read_until(
            b"B8_SESSION_TEARDOWN: capability/event cleanup PASS",
            timeout=30, start_offset=start,
        )
        record("session teardown", teardown)
    except TimeoutError as error:
        record("UART completion", False, str(error))
        if args.gdb_log:
            from qemu_snapshot import snapshot
            args.gdb_log.write_text(snapshot(debug_kernel, debug_port, debug_tty))
            print(f"  retained read-only snapshot: {args.gdb_log}")
        return 1
    finally:
        session.kill()
        if args.uart_log:
            args.uart_log.write_bytes(session.output)

    failed = sum(not passed for _, passed, _ in checks)
    print(f"\nSilt rootfs acceptance: {len(checks) - failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
