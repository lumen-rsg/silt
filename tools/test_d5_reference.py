#!/usr/bin/env python3
"""Run the shipped profile cases unchanged with the pinned Linux Dash build."""
import os
from pathlib import Path
import subprocess
import sys

shell = Path(sys.argv[1]).resolve()
script = Path(sys.argv[2]).resolve()
environment = dict(os.environ, D5_SHELL=str(shell), LC_ALL='C')
result = subprocess.run([str(shell), str(script)], env=environment,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
sys.stdout.buffer.write(result.stdout)
sys.stderr.buffer.write(result.stderr)
assert result.returncode == 0 and result.stdout.count(b'D5_CASE_PASS:') == 20
assert b'D5_CONFORMANCE: 20 passed\n' in result.stdout and not result.stderr
