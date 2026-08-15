#!/usr/bin/env python3
"""Boot Neva with the Silt rootfs and exercise the first userland surface."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def command_output(session, runner, command: str, timeout: int = 30) -> bytes:
    start = len(session.output)
    session.send(command)
    if not session.read_until(runner.PROMPT, timeout=timeout, start_offset=start):
        raise TimeoutError(f"prompt did not return after {command!r}")
    lines = session.output[start:].replace(b"\r", b"").split(b"\n")
    command_bytes = command.encode()
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
            output = command_output(session, runner, command)
            missing = [marker.decode(errors="replace")
                       for marker in markers if marker not in output]
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
        if not session.read_until(runner.PROMPT, timeout=45):
            print(session.output.decode(errors="replace")[-10000:])
            return 1
        boot_markers = (
            b"B2_INITD_READY: PASS",
            b"B3_INITD_READY: PASS",
            b"B5_EL0_PROVIDERS: fat32/ext4/nevfs PASS",
            b"B5_VFSD: namespace/data PASS",
            b"B8_SESSION_READY: uid-scoped manager/catalog PASS",
        )
        session.read_until(boot_markers[1], timeout=45)
        for marker in boot_markers:
            record("boot:" + marker.decode().split(":", 1)[0],
                   marker in session.output)
        if any(marker not in session.output for marker in boot_markers):
            failures = [line for line in session.output.decode(
                errors="replace").splitlines() if "FAIL" in line]
            if failures:
                print("  boot diagnostics: " + " | ".join(failures[-8:]))

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
