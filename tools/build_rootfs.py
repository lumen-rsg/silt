#!/usr/bin/env python3
"""Assemble the deterministic Silt R0 root filesystem image."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--formatter", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--staging", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--app", action="append", nargs=2, default=[])
    args = parser.parse_args()

    if not args.formatter.is_file():
        raise FileNotFoundError(f"missing NevFS formatter: {args.formatter}")
    if not args.staging.is_dir():
        raise FileNotFoundError(f"missing rootfs staging directory: {args.staging}")
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if manifest.get("format") != "nevfs-v1":
        raise ValueError("Silt R0 requires the nevfs-v1 image format")
    size_mib = manifest.get("size_mib")
    if not isinstance(size_mib, int) or size_mib < 4 or size_mib > 64:
        raise ValueError("rootfs size_mib must be an integer in [4, 64]")

    expected_apps = manifest.get("applications")
    if not isinstance(expected_apps, list):
        raise ValueError("rootfs applications must be a JSON list")
    if any(not isinstance(name, str) or not name or "/" in name
           for name in expected_apps):
        raise ValueError("rootfs applications must be unique executable names")
    if (expected_apps != sorted(expected_apps)
            or len(set(expected_apps)) != len(expected_apps)):
        raise ValueError("rootfs applications must be sorted and unique")
    supplied = {name: Path(path) for name, path in args.app}
    if len(args.app) != len(expected_apps) or sorted(supplied) != expected_apps:
        raise ValueError("rootfs application inputs do not match manifest")
    missing = [str(path) for path in supplied.values() if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing rootfs applications: " + ", ".join(missing))

    required_paths = manifest.get("required_paths")
    if not isinstance(required_paths, list):
        raise ValueError("rootfs required_paths must be a JSON list")
    if any(not isinstance(path, str) or not path or path.startswith("/")
           or ".." in Path(path).parts for path in required_paths):
        raise ValueError("rootfs required_paths must be relative paths")
    if (required_paths != sorted(required_paths)
            or len(set(required_paths)) != len(required_paths)):
        raise ValueError("rootfs required_paths must be sorted and unique")
    staged_or_seeded = {
        "boot/etc/hello.txt",
        "etc/group",
        "etc/passwd",
        "etc/shadow",
    }
    staged_or_seeded.update(
        str(path.relative_to(args.staging))
        for path in sorted(args.staging.rglob("*")) if path.is_file()
    )
    absent = sorted(set(required_paths) - staged_or_seeded)
    if absent:
        raise FileNotFoundError("missing required rootfs paths: " + ", ".join(absent))

    command = [
        sys.executable,
        str(args.formatter),
        str(args.output),
        str(size_mib),
        "--populate",
        str(args.staging),
    ]
    for name in expected_apps:
        command.extend(["--extra-app", name, str(supplied[name])])
    environment = os.environ.copy()
    environment["SOURCE_DATE_EPOCH"] = "0"
    subprocess.run(command, check=True, env=environment)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
