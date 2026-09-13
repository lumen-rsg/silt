"""Shared Dash rejection/recovery cases; Silt quota vs Linux fork injection."""


def run_resource_cases(command, record, descriptors, *, suspended):
    def check(name, passed, output=b''):
        record('resource: ' + name, passed, '' if passed else repr(output[-2000:]))
        if not passed:
            raise AssertionError('resource case failed: ' + name)

    baseline = descriptors()
    command('rx_hold() { echo RX_READY_$1; while :; do :; done; }')
    command('rx_stage() { echo RX_STAGE_RAN >/dev/tty; }')
    for admitted in (0, 1, 3, 5):
        command('rx_pids=')
        holders = 6 - admitted
        for index in range(holders):
            command(f'rx_hold {index} & rx_pids="$rx_pids $!"',
                    ready=f'RX_READY_{index}\n'.encode())
        failures = [' | '.join(['rx_stage'] * max(2, admitted + 1)),
                    ' | '.join(['rx_stage'] * (admitted + 2))]
        if admitted == 0:
            failures += ['(exit 9)', "dash -c ':'", '(exit 9) &']
        for repeat in range(2):
            for index, text in enumerate(failures):
                output = command(text, rejected=True, reject_after=admitted)
                check(f'{admitted} slots attempt {repeat + 1}.{index + 1} rejected',
                      b'Cannot fork\n' in output and (not suspended or b'RX_STAGE_RAN' not in output), output)
                output = command('echo RX_STATUS=$?; jobs')
                check('status and existing jobs survive rejection',
                      b'RX_STATUS=2' in output.splitlines()
                      and output.count(b'rx_hold') == holders
                      and b'Stopped' not in output and b'rx_stage' not in output, output)
        output = command('kill -TERM $rx_pids; s=$?; wait; echo RX_REAP=$s:$?; unset rx_pids')
        check(f'{admitted} slots existing children reap', b'RX_REAP=0:0' in output.splitlines(), output)
        current = descriptors()
        check(f'{admitted} slots descriptor capacity restored', current == baseline,
              f'baseline={baseline}, current={current}'.encode())
        output = command('printf "RX_RECOVERED\\n" | { read x; echo "$x"; }')
        check(f'{admitted} slots pipeline works after release', b'RX_RECOVERED' in output.splitlines(), output)
        output = command('jobs')
        check(f'{admitted} slots job table empty', b'rx_hold' not in output and b'rx_stage' not in output, output)
