#!/usr/bin/env python3
"""Boot Neva with the Silt rootfs and exercise the first userland surface."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def command_output(
    session, runner, command: str, markers: tuple[bytes, ...], timeout: int = 30,
) -> bytes:
    start = len(session.output)
    session.send(command)
    if not session.read_until(runner.PROMPT, timeout=timeout, start_offset=start):
        tail = session.output[start:].decode(errors="replace")[-2000:]
        raise TimeoutError(
            f"prompt did not return after {command!r}; output={tail!r}"
        )
    # nsh writes its prompt directly while Silt applications write through
    # ttyd, so the prompt can overtake the final service-backed output bytes.
    # Wait for declared output after the prompt as well as before it.
    command_bytes = command.encode()
    command_position = session.output.find(command_bytes, start)
    marker_offset = command_position + len(command_bytes) \
        if command_position >= 0 else start
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
    parser.add_argument("--rootfs", type=Path, required=True)
    parser.add_argument("--smp", type=int, default=4)
    args = parser.parse_args()

    neva_source = args.neva_source.resolve()
    neva_build = args.neva_build.resolve()
    rootfs = args.rootfs.resolve()
    if not rootfs.is_file():
        parser.error(f"rootfs image does not exist: {rootfs}")
    if not (neva_build / "neva.elf").is_file():
        parser.error(f"Neva kernel does not exist: {neva_build / 'neva.elf'}")

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

    session = runner.QemuSession()
    checks: list[tuple[str, bool, str]] = []

    def record(name: str, passed: bool, detail: str = "") -> None:
        checks.append((name, passed, detail))
        suffix = f": {detail}" if detail else ""
        print(f"  [{'PASS' if passed else 'FAIL'}] {name}{suffix}")

    def run(command: str, markers: tuple[bytes, ...], name: str) -> None:
        try:
            output = command_output(session, runner, command, markers)
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
        if any(marker not in session.output for marker in boot_markers):
            failures = [line for line in session.output.decode(
                errors="replace").splitlines() if "FAIL" in line]
            if failures:
                print("  boot diagnostics: " + " | ".join(failures[-8:]))

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
            "{ IFS= read -r line; printf \"%s\\n\" \"$line\"; } | "
            "{ IFS= read -r line; printf \"%s\\n\" \"$line\"; }'",
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

        print("\n=== Session teardown ===")
        start = len(session.output)
        session.send("exit")
        teardown = session.read_until(
            b"B8_SESSION_TEARDOWN: capability/event cleanup PASS",
            timeout=30, start_offset=start,
        )
        record("session teardown", teardown)
    finally:
        session.kill()

    failed = sum(not passed for _, passed, _ in checks)
    print(f"\nSilt rootfs acceptance: {len(checks) - failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
