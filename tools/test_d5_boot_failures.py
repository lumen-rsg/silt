#!/usr/bin/env python3
"""Build separate failed-startup images and require a usable retained nsh."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
from test_d5_image import build_command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('neva-source', 'neva-build', 'silt-build', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--smp', type=int, choices=(1, 4, 8), default=8)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    cases = (
        ('missing-program', 'boot/session', 'no-d5-program\n'),
        ('script-exit', 'boot/session.sh', '#!/bin/sh\nexit 42\n'),
        ('malformed-policy', 'boot/session', 'sh /boot/session.sh\nsecond command\n'),
    )
    for name, path, data in cases:
        output = args.output/name
        staging = output/'staging'
        shutil.copytree(root/'rootfs', staging, dirs_exist_ok=True)
        (staging/path).write_text(data)
        image = output/'rootfs.img'
        with (output/'build.log').open('w') as log:
            subprocess.run(build_command(args.neva_source, args.neva_build,
                args.silt_build, staging, image), check=True, stdout=log)
        subprocess.run([sys.executable, str(root/'tools/test_d5_release.py'),
            '--neva-source', str(args.neva_source), '--neva-build', str(args.neva_build),
            '--rootfs', str(image), '--smp', str(args.smp), '--output', str(output),
            '--expect-recovery'], check=True)
        print('D5 failed startup recovery: '+name+' PASS', flush=True)


if __name__ == '__main__':
    main()
