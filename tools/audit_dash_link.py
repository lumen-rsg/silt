#!/usr/bin/env python3
"""Reject a dash process image that retains unresolved external symbols."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--nm", default="aarch64-none-elf-nm")
    args = parser.parse_args()

    result = subprocess.run(
        [args.nm, "--undefined-only", str(args.image)],
        check=True,
        capture_output=True,
        text=True,
    )
    unresolved = result.stdout.strip()
    if unresolved:
        print("dash image retains unresolved symbols:")
        print(unresolved)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
