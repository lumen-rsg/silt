#!/usr/bin/env python3
"""Build an unpatched Linux reference from the same verified GNU archive."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/coreutils-reference'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    subprocess.run(['python3', str(root / 'tools/prepare_coreutils.py')], check=True)
    lock = json.loads((root / 'ports/coreutils/upstream.lock').read_text())
    archive = root / 'subprojects/packagecache' / f"coreutils-{lock['version']}.tar.xz"
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == lock['sha256']
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = output / f"coreutils-{lock['version']}"
    if not source.exists():
        with tarfile.open(archive) as tar:
            tar.extractall(output, filter='data')
    build = output / 'build'
    build.mkdir(exist_ok=True)
    if not (build / 'Makefile').exists():
        subprocess.run([str(source / 'configure'), '--disable-nls', '--disable-gcc-warnings',
                        '--without-selinux', '--without-libgmp'], cwd=build, check=True)
    # Direct src/<command> targets do not depend on all generated gnulib headers.
    # Generate the release's declared prerequisites before compiling any objects.
    rules = build / 'silt-reference.mk'
    rules.write_text('.PHONY: silt-prerequisites\nsilt-prerequisites: $(BUILT_SOURCES)\n')
    subprocess.run(['make', '-j4', '-f', 'Makefile', '-f', str(rules), 'silt-prerequisites'],
                   cwd=build, check=True)
    subprocess.run(['make', '-j4', 'src/echo', 'src/basename', 'src/dirname', 'src/cat', 'src/head', 'src/tail', 'src/wc', 'src/pwd', 'src/printenv', 'src/yes'], cwd=build, check=True)
    print(f'Pinned Linux reference: {build / "src"}')


if __name__ == '__main__':
    main()
