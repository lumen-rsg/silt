"""Original C0 differential cases; upstream command sources supply the oracle."""


def cases():
    result = []
    def add(name, args, **extras):
        result.append(dict(name=name, args=args, **extras))
    for name, args in [
        ('empty', []), ('words', ['one', '', 'two words']),
        ('nonewline', ['-n', 'word']), ('combined', ['-ne', 'a\\nb']),
        ('disable', ['-e', '-E', 'a\\nb']), ('invalid', ['-nx', 'word']),
        ('dash', ['-', 'word']), ('double-dash', ['--', 'word']),
        ('help-operand', ['--help', 'word']), ('hex', ['-e', '\\x41\\x4a']),
        ('octal', ['-e', '\\0101\\101']), ('nul', ['-e', 'a\\0b']),
        ('stop', ['-e', 'a\\cignored', 'later']),
        ('unknown-escape', ['-e', '\\q\\xZ']), ('slash', ['-e', '\\\\']),
        ('controls', ['-ne', '\\a\\b\\e\\f\\n\\r\\t\\v']),
        ('trailing-slash', ['-e', 'end\\']), ('tabs', ['-e', 'a\\tb']),
    ]:
        add('echo-' + name, ['echo'] + args)
    add('echo-posix', ['echo', '-e', 'a\\nb'], env='POSIXLY_CORRECT=1')
    add('echo-posix-n', ['echo', '-n', 'a\\nb'], env='POSIXLY_CORRECT=1')
    for name, operand in [('empty', ''), ('root', '/'), ('double-root', '//'),
                          ('slashes', '////'), ('plain', 'file'), ('relative', './file'),
                          ('trailing', 'a/b///'), ('spaces', '/two words/a b'),
                          ('dotdot', 'a/../b'), ('unicode-bytes', '/café/été')]:
        add('basename-' + name, ['basename', operand])
        add('dirname-' + name, ['dirname', operand])
    for name, args in [
        ('suffix', ['/a/file.c', '.c']), ('whole-suffix', ['file', 'file']),
        ('long-suffix', ['f', 'long']), ('empty-suffix', ['f', '']),
        ('multiple', ['-a', 'a/b', 'c/d']), ('suffix-option', ['-s', '.c', 'a.c', 'b.c']),
        ('long-option', ['--suffix=.c', 'a.c']), ('abbreviation', ['--suff=.c', 'a.c']),
        ('zero', ['-az', 'a/b', 'c/d']), ('dash-operand', ['--', '-a']),
        ('missing', []), ('extra', ['a', 'b', 'c']), ('bad-option', ['--bogus']),
        ('missing-suffix', ['-s']), ('no-permute', ['a/b', '-z']),
    ]:
        add('basename-' + name, ['basename'] + args)
    for name, args in [
        ('multiple', ['a/b', 'c/d']), ('zero', ['-z', 'a/b', 'c/d']),
        ('permute', ['a/b', '-z', 'c/d']), ('long-option', ['--zero', 'a/b']),
        ('dash-operand', ['--', '-a']), ('missing', []), ('bad-option', ['--bogus']),
        ('unexpected-value', ['--zero=x', 'a/b']),
    ]:
        add('dirname-' + name, ['dirname'] + args)
    for utility in ['echo', 'basename', 'dirname']:
        add(utility + '-help', [utility, '--help'], prefix=b'Usage: ')
        add(utility + '-version', [utility, '--version'], prefix=(utility + ' (GNU coreutils) 9.11\n').encode())
        add(utility + '-closed-output', [utility, 'a/b'], redirect=' >&-')
    return result
