"""Bounded read-only observation of a shell's signal-suspend wait."""

from pathlib import Path
import subprocess
import time


def linux_suspend(pid):
    deadline = time.monotonic() + 5
    last = ""
    while time.monotonic() < deadline:
        last = Path(f"/proc/{pid}/wchan").read_text().strip()
        status = Path(f"/proc/{pid}/status").read_text()
        blocked = int(next(line.split()[1] for line in status.splitlines()
                           if line.startswith("SigBlk:")), 16)
        if "sigsuspend" in last and not (blocked & (1 << 1)):
            return f"WAIT_OBSERVED linux pid={pid} wchan={last}"
        time.sleep(0.005)
    raise TimeoutError(f"Linux shell did not enter sigsuspend: pid={pid} wchan={last}")


def guest_suspend(kernel, port, pid):
    script = f"""
import gdb
for i in range(int(gdb.parse_and_eval('thread_count'))):
    t = gdb.parse_and_eval('threads[%d]' % i)
    if not int(t) or int(t['id']) != {int(pid)}:
        continue
    if (int(t['state']) == int(gdb.parse_and_eval('THREAD_BLOCKED'))
            and int(t['wait']['reason']) == int(gdb.parse_and_eval('BLOCK_REASON_SIGNAL'))
            and int(t['signals']['suspended_mask']) == 1
            and not int(t['signals']['in_signal'])
            and not (int(t['signals']['blocked']) & (1 << 2))
            and int(t['running_cpu']) == 0xffffffff
            and not any(int(gdb.parse_and_eval('g_cpu_locals[%d].current_thread' % c)) == int(t)
                        or int(gdb.parse_and_eval('g_cpu_locals[%d].retired_thread' % c)) == int(t)
                        for c in range(int(gdb.parse_and_eval('g_cpu_count'))))):
        print('WAIT_OBSERVED neva pid={int(pid)} state=BLOCK_REASON_SIGNAL INT=unblocked')
    break
"""
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        result = subprocess.run(
            ['gdb', '-q', '-nx', '-batch', str(kernel),
             '-ex', 'set may-call-functions off', '-ex', 'set may-write-memory off',
             '-ex', 'set may-write-registers off', '-ex', 'set remotetimeout 3',
             '-ex', f'target remote 127.0.0.1:{int(port)}',
             '-ex', 'python exec(' + repr(script) + ')', '-ex', 'detach'],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode or 'Error' in result.stderr:
            raise RuntimeError(result.stdout + result.stderr)
        for line in result.stdout.splitlines():
            if line.startswith('WAIT_OBSERVED '):
                return line
        time.sleep(0.025)
    raise TimeoutError(f'Neva shell did not enter signal suspension: pid={pid}')
