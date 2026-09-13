#!/usr/bin/env python3
"""Actual-image pipeline admission refusals and interactive recovery in QEMU."""

import argparse
from pathlib import Path
import re
import socket
import shutil
import sys

from pipeline_fault import PipelineFault
from release_boot import enter_recovery


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neva-source', type=Path, required=True)
    parser.add_argument('--neva-build', type=Path, required=True)
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--kernel', type=Path)
    parser.add_argument('--smp', type=int, default=4)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--phase', choices=('handoff', 'resume'))
    parser.add_argument('--ordinal', type=int, choices=range(1, 4))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    kernel_source = (args.kernel or args.neva_build / 'neva.elf').resolve()
    kernel = (args.output / 'kernel.elf').resolve()
    if kernel_source != kernel:
        shutil.copy2(kernel_source, kernel)
    sys.path.insert(0, str(args.neva_source.resolve() / 'tools'))
    sys.argv = ['test_runner.py', str(args.neva_build.resolve()), '--skip-build',
                f'--smp={args.smp}', f'--nevfs-image={args.rootfs.resolve()}']
    import test_runner as runner
    runner.KERNEL_NAME = str(kernel)
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        port = listener.getsockname()[1]
    original = runner.qemu_command
    runner.qemu_command = lambda media: original(media) + ['-gdb', f'tcp:127.0.0.1:{port}']
    session = runner.QemuSession()
    injector = None
    checks = 0

    def check(condition, description):
        nonlocal checks
        if not condition:
            raise AssertionError(description + '\n' + session.output[-2500:].decode(errors='replace'))
        checks += 1
        print('PASS ' + description, flush=True)

    def command(text):
        if len(text.encode()) > 126:
            raise ValueError('command too long')
        start = len(session.output)
        session.send(text)
        if not session.read_until(text.encode(), timeout=15, start_offset=start):
            raise TimeoutError('echo: ' + text)
        after = session.output.index(text.encode(), start) + len(text.encode())
        if not session.read_until(b'A4> ', timeout=30, start_offset=after):
            raise TimeoutError('prompt: ' + text)
        return session.output[after:].replace(b'\r', b'')

    def descriptors():
        output = command('check-cleanup fds')
        match = re.search(rb'^RX_FDS=(\d+)$', output, re.MULTILINE)
        if not match:
            raise AssertionError('missing descriptor count: ' + repr(output))
        return int(match[1])

    try:
        session.start()
        enter_recovery(session, runner)
        check(b'B3_INITD_READY: PASS' in session.output, 'service graph ready')
        start = len(session.output)
        session.send('')
        check(session.read_until(runner.PROMPT, timeout=15, start_offset=start), 'recovery prompt')
        start = len(session.output)
        session.send('dash -i')
        check(session.read_until(b'$ ', timeout=15, start_offset=start), 'Dash launched')
        command("PS1='A4> '")
        output = command('echo ADMISSION_PID=$$')
        pid = int(re.search(rb'ADMISSION_PID=(\d+)', output)[1])
        command('ad_hold() { while :; do :; done; }')
        command('ad_hold & keep=$!')
        baseline = descriptors()
        cases = [('handoff', 1, False, 'pipeline')]
        cases += [('resume', ordinal, bg, 'pipeline') for bg in (False, True) for ordinal in (1, 2, 3)]
        cases += [('handoff', 1, False, 'subshell'), ('resume', 1, False, 'subshell'),
                  ('resume', 1, True, 'subshell')]
        cases += [('handoff', 1, False, 'external'), ('resume', 1, False, 'external'),
                  ('resume', 1, True, 'external')]
        for repeat in range(2):
            for phase, ordinal, background, kind in cases:
                if (args.phase and phase != args.phase) or (args.ordinal and ordinal != args.ordinal):
                    continue
                name = f'{repeat}-{phase}-{ordinal}-bg{int(background)}-{kind}'
                injector = PipelineFault(kernel, port, pid, phase, ordinal,
                                         args.output / (name + '.gdb.log'))
                text = ("dash -c 'while :; do :; done'" if kind == 'external' else
                        '(ad_hold)' if kind == 'subshell' else 'ad_hold | ad_hold | ad_hold')
                if background:
                    text += ' &'
                output = command(text)
                evidence = injector.finish()
                injector.close()
                injector = None
                check(f'FAULT_INJECTED phase={phase} ordinal={ordinal} pid={pid}' in evidence,
                      name + ' injected')
                check(b'Cannot start ' in output, name + ' reports refusal')
                output = command('echo AD_STATUS=$?; jobs')
                check(b'AD_STATUS=2\n' in output and output.count(b'ad_hold') == 1
                      and b'Stopped' not in output, name + ' status and unrelated job')
                check(baseline == descriptors(), name + ' descriptors restored')
                output = command('printf "AD_PAYLOAD\\n" | check-cleanup copy')
                check(b'AD_PAYLOAD\n' in output, name + ' foreground pipeline refill')
        output = command('kill -TERM $keep; wait $keep; echo AD_REAP=$?; jobs')
        check(b'AD_REAP=143\n' in output, 'unrelated child reaped')
        check(b'ad_hold' not in command('jobs'), 'job table empty')
        # Fill the six-child session quota after all refusals to detect leaked
        # suspended or already-resumed construction members, beyond Dash jobs.
        command('ad_pids=')
        for index in range(6):
            output = command('ad_hold & ad_pids="$ad_pids $!"')
            check(b'Cannot ' not in output, f'process quota refill {index + 1}')
        output = command('kill -TERM $ad_pids; wait; echo AD_DONE=$?')
        check(b'AD_DONE=0\n' in output, 'quota refill reaped')
        print(f'{checks} admission checks passed', flush=True)
        return 0
    finally:
        if injector:
            injector.close()
        (args.output / 'uart.log').write_bytes(session.output)
        session.kill()


if __name__ == '__main__':
    raise SystemExit(main())
