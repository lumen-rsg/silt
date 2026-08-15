#!/usr/bin/env python3
"""Build and launch Silt on the sibling Neva microkernel checkout."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

from setup_meson import find_compiler


def resolved(path: Path) -> Path:
    return path.expanduser().resolve()


def run_checked(command: list[str], cwd: Path, environment: dict[str, str]) -> None:
    print("+ " + shlex.join(command), flush=True)
    subprocess.run(command, cwd=cwd, env=environment, check=True)


def require_file(path: Path, description: str) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"{description} is missing: {path}")


def build_projects(
    silt_source: Path,
    silt_build: Path,
    neva_source: Path,
    neva_build: Path,
) -> None:
    compiler = find_compiler()
    environment = os.environ.copy()
    environment["PATH"] = str(compiler.parent) + os.pathsep + environment.get(
        "PATH", ""
    )

    run_checked(
        [sys.executable, str(neva_source / "tools" / "setup_meson.py"), str(neva_build)],
        neva_source,
        environment,
    )
    run_checked(["meson", "compile", "-C", str(neva_build)], neva_source, environment)
    neva_source_option = os.path.relpath(neva_source, silt_source)
    neva_build_option = os.path.relpath(neva_build, silt_source)
    run_checked(
        [
            sys.executable,
            str(silt_source / "tools" / "setup_meson.py"),
            str(silt_build),
            f"-Dneva_source_dir={neva_source_option}",
            f"-Dneva_build_dir={neva_build_option}",
        ],
        silt_source,
        environment,
    )
    run_checked(["meson", "compile", "-C", str(silt_build)], silt_source, environment)


def copy_boot_media(
    media_dir: Path,
    neva_build: Path,
    rootfs: Path,
) -> tuple[Path, Path, Path, Path]:
    scratch = media_dir / "scratch.img"
    disk = media_dir / "disk.img"
    ext4 = media_dir / "ext4.img"
    silt = media_dir / "silt-rootfs.img"

    with scratch.open("wb") as output:
        output.truncate(1024 * 1024)
    shutil.copyfile(neva_build / "disk.img", disk)
    shutil.copyfile(neva_build / "ext4.img", ext4)
    shutil.copyfile(rootfs, silt)
    return scratch, disk, ext4, silt


def qemu_command(
    qemu: str,
    kernel: Path,
    scratch: Path,
    disk: Path,
    ext4: Path,
    rootfs: Path,
    smp: int,
    memory: str,
    gui: bool,
    debug: bool,
    extra_arguments: list[str],
) -> list[str]:
    command = [
        qemu,
        "-M", "virt,gic-version=3",
        "-cpu", "cortex-a72",
        "-smp", str(smp),
        "-m", memory,
        "-kernel", str(kernel),
        # QEMU assigns virtio-mmio transports in reverse command-line order.
        "-drive", f"file={scratch},format=raw,if=none,id=hd3",
        "-device", "virtio-blk-device,drive=hd3",
        "-drive", f"file={disk},format=raw,if=none,id=hd0",
        "-device", "virtio-blk-device,drive=hd0",
        "-drive", f"file={ext4},format=raw,if=none,id=hd1",
        "-device", "virtio-blk-device,drive=hd1",
        "-drive", f"file={rootfs},format=raw,if=none,id=hd2",
        "-device", "virtio-blk-device,drive=hd2",
    ]
    if gui:
        command.extend([
            "-device", "virtio-gpu-device",
            "-device", "virtio-keyboard-device",
            "-device", "virtio-mouse-device",
            "-serial", "stdio",
        ])
    else:
        command.append("-nographic")
    if debug:
        command.extend(["-d", "int,cpu_reset"])
    command.extend([
        "-netdev", "user,id=net0",
        "-device", "virtio-net-device,netdev=net0",
    ])
    command.extend(extra_arguments)
    return command


def main() -> int:
    silt_source = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(
        description="Build Neva and Silt, then boot Silt in QEMU.",
    )
    parser.add_argument(
        "--silt-build",
        type=Path,
        default=silt_source / "build",
        help="Silt Meson build directory (default: %(default)s)",
    )
    parser.add_argument(
        "--neva-source",
        type=Path,
        default=silt_source.parent / "neva-microkernel",
        help="Neva source checkout (default: sibling neva-microkernel)",
    )
    parser.add_argument(
        "--neva-build",
        type=Path,
        help="Neva Meson build directory (default: <neva-source>/build-meson)",
    )
    parser.add_argument(
        "--rootfs",
        type=Path,
        help="Silt rootfs image (default: <silt-build>/silt-rootfs.img)",
    )
    parser.add_argument("--smp", type=int, default=4, help="virtual CPU count")
    parser.add_argument("--memory", default="128M", help="QEMU guest memory")
    parser.add_argument("--gui", action="store_true", help="enable the QEMU display")
    parser.add_argument("--debug", action="store_true", help="enable QEMU interrupt logging")
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="launch existing build artifacts without compiling",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="prepare and print the exact QEMU command without booting",
    )
    parser.add_argument(
        "--qemu",
        default="qemu-system-aarch64",
        help="QEMU executable (default: %(default)s)",
    )
    parser.add_argument(
        "--qemu-arg",
        action="append",
        default=[],
        help="append one raw QEMU argument; repeat as needed",
    )
    parser.add_argument(
        "--console-tty",
        action="store_true",
        help=argparse.SUPPRESS,
    )
    args = parser.parse_args()

    if args.smp < 1 or args.smp > 8:
        parser.error("--smp must be between 1 and 8")

    silt_build = resolved(args.silt_build)
    neva_source = resolved(args.neva_source)
    neva_build = resolved(args.neva_build or neva_source / "build-meson")
    rootfs = resolved(args.rootfs or silt_build / "silt-rootfs.img")

    require_file(neva_source / "tools" / "setup_meson.py", "Neva setup helper")
    if not args.skip_build:
        build_projects(silt_source, silt_build, neva_source, neva_build)

    kernel = neva_build / "neva.elf"
    require_file(kernel, "Neva kernel")
    require_file(neva_build / "disk.img", "Neva FAT test image")
    require_file(neva_build / "ext4.img", "Neva ext4 test image")
    require_file(rootfs, "Silt rootfs image")
    if shutil.which(args.qemu) is None and not Path(args.qemu).is_file():
        raise FileNotFoundError(f"QEMU executable was not found: {args.qemu}")

    neva_tools = str(neva_source / "tools")
    if neva_tools not in sys.path:
        sys.path.insert(0, neva_tools)
    from qemu_dtb import prepared_qemu_command

    print("\nSilt on Neva", flush=True)
    print(f"  kernel: {kernel}")
    print(f"  rootfs: {rootfs}")
    print(f"  machine: {args.smp} vCPU, {args.memory}, {'GUI' if args.gui else 'terminal'}")
    if not args.gui:
        print("  exit QEMU: Ctrl-A, then X")

    with tempfile.TemporaryDirectory(prefix="silt-qemu-media-") as temporary:
        scratch, disk, ext4, writable_rootfs = copy_boot_media(
            Path(temporary), neva_build, rootfs
        )
        command = qemu_command(
            args.qemu,
            kernel,
            scratch,
            disk,
            ext4,
            writable_rootfs,
            args.smp,
            args.memory,
            args.gui,
            args.debug,
            args.qemu_arg,
        )
        with prepared_qemu_command(command, kernel) as prepared:
            print("\n+ " + shlex.join(prepared), flush=True)
            if args.dry_run:
                return 0
            terminal_input = None
            try:
                if args.console_tty:
                    terminal_input = open("/dev/tty", "rb", buffering=0)
                return subprocess.call(prepared, stdin=terminal_input)
            finally:
                if terminal_input is not None:
                    terminal_input.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2) from None
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode or 1) from None
    except KeyboardInterrupt:
        print("\nlaunch interrupted", file=sys.stderr)
        raise SystemExit(130) from None
