#!/usr/bin/env python3
"""Run shared resource recovery cases with a bounded Linux fork fault injector."""

from pathlib import Path
import subprocess
import tempfile

from test_dash_jobs import main


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='dash-resource-') as directory:
        temporary = Path(directory)
        library = temporary / 'fork-fault.so'
        control = temporary / 'control'
        control.write_text('0 -1\n')
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(Path(__file__).with_name('dash_fork_fault.c')), '-ldl',
                        '-o', str(library)], check=True)
        main(resource_control=control, resource_library=library)
