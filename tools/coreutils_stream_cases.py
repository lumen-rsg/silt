"""GNU stream command cases shared with the pinned Linux reference."""


def cases():
    cases = []
    def add(name, utility, arguments, source='lines', pipe=False):
        cases.append(dict(name=name, utility=utility, arguments=arguments, source=source, pipe=pipe))
    for source in ['lines', 'binary', 'long', 'empty']:
        for pipe in [False, True]:
            for utility in ['cat', 'head', 'tail', 'wc']:
                add(f'{utility}-{source}-{"pipe" if pipe else "file"}', utility, [], source, pipe)
    for options in [['-n'], ['-b'], ['-s'], ['-A'], ['-E'], ['-T'], ['-v'], ['-benst'], ['-u']]:
        for source in ['lines', 'binary']:
            add('cat-'+''.join(options)+'-'+source, 'cat', options, source)
    for utility in ['head', 'tail']:
        for options in [['-n', '0'], ['-n', '1'], ['-n', '2'], ['-n', '+2'], ['-n', '-2'],
                        ['-c', '0'], ['-c', '1'], ['-c', '4097'], ['-c', '+3'], ['-c', '-3'],
                        ['-c', '1K'], ['-c', '1kB'], ['-c', '1KiB'], ['-c', 'K'],
                        ['-c', '999999999999999999999999999'], ['-2'], ['-z', '-n', '3']]:
            for pipe in [False, True]:
                add(utility+'-'+','.join(options)+f'-{pipe}', utility, options, 'binary', pipe)
        for options in [['-n', '-2'], ['-c', '-4097'], ['-n', '999999999999999999999999']]:
            for pipe in [False, True]:
                add(utility+'-long-'+','.join(options)+f'-{pipe}', utility, options, 'long', pipe)
    for options in [['-c'], ['-l'], ['-w'], ['-m'], ['-L'], ['-cl'], ['-cmwlL'],
                    ['--total=always'], ['--total=only'], ['--total=never']]:
        for source in ['lines','binary','long']:
            add('wc-'+','.join(options)+'-'+source, 'wc', options, source)
    return cases
