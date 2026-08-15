#!/usr/bin/env python3
"""Prepare dash and configure a freestanding Silt ARM64 Meson build."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess


TOOLCHAIN_VERSION = "15.3.rel1"
TOOL_PREFIX = "aarch64-none-elf-"


def find_compiler() -> Path:
    cross_compile = os.environ.get("CROSS_COMPILE")
    if cross_compile:
        candidate = shutil.which(cross_compile + "gcc")
        if candidate:
            return Path(candidate).resolve()

    arm_root = os.environ.get("ARM_GNU_ROOT")
    if arm_root:
        candidate = Path(arm_root).expanduser() / "bin" / (TOOL_PREFIX + "gcc")
        if candidate.is_file():
            return candidate.resolve()

    candidate = shutil.which(TOOL_PREFIX + "gcc")
    if candidate:
        return Path(candidate).resolve()

    toolchain_root = Path.home() / ".local" / "share" / "toolchains"
    pinned = toolchain_root / (
        f"arm-gnu-toolchain-{TOOLCHAIN_VERSION}-{platform.machine()}-"
        "aarch64-none-elf"
    ) / "bin" / (TOOL_PREFIX + "gcc")
    if pinned.is_file():
        return pinned.resolve()

    raise FileNotFoundError(
        "aarch64-none-elf-gcc was not found; set ARM_GNU_ROOT or CROSS_COMPILE"
    )


def meson_quote(value: Path | str) -> str:
    return "'" + str(value).replace("\\", "\\\\").replace("'", "\\'") + "'"


def write_cross_file(build_dir: Path, compiler: Path) -> Path:
    bin_dir = compiler.parent
    tools = {
        "c": compiler,
        "ld": bin_dir / (TOOL_PREFIX + "ld"),
        "ar": bin_dir / (TOOL_PREFIX + "ar"),
        "strip": bin_dir / (TOOL_PREFIX + "strip"),
    }
    missing = [str(path) for path in tools.values() if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing Arm GNU tools: " + ", ".join(missing))

    build_dir.mkdir(parents=True, exist_ok=True)
    cross_file = build_dir / "silt-cross.ini"
    lines = ["[binaries]"]
    lines.extend(f"{name} = {meson_quote(path)}" for name, path in tools.items())
    lines.extend(
        [
            "",
            "[properties]",
            "needs_exe_wrapper = true",
            "",
            "[host_machine]",
            "system = 'none'",
            "cpu_family = 'aarch64'",
            "cpu = 'armv8-a'",
            "endian = 'little'",
            "",
            "[built-in options]",
            "c_std = 'gnu11'",
            "b_pie = false",
            "b_staticpic = false",
            "",
        ]
    )
    cross_file.write_text("\n".join(lines), encoding="utf-8")
    return cross_file


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", nargs="?", default="build")
    parser.add_argument("meson_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    source_dir = Path(__file__).resolve().parent.parent
    build_dir = Path(args.build_dir).expanduser().resolve()
    subprocess.run([str(source_dir / "tools" / "prepare_dash.sh")], check=True)

    compiler = find_compiler()
    cross_file = write_cross_file(build_dir, compiler)
    command = [
        "meson",
        "setup",
        str(build_dir),
        str(source_dir),
        "--cross-file",
        str(cross_file),
    ]
    if (build_dir / "meson-private" / "coredata.dat").is_file():
        command.append("--reconfigure")
    command.extend(args.meson_args)

    environment = os.environ.copy()
    environment["PATH"] = str(compiler.parent) + os.pathsep + environment.get(
        "PATH", ""
    )
    return subprocess.call(command, cwd=source_dir, env=environment)


if __name__ == "__main__":
    raise SystemExit(main())
