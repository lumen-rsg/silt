"""Enter the retained recovery shell after verifying Silt's normal startup."""

SESSION_MARKER = b'SILT_SESSION_READY: sh startup complete'
SESSION_PROMPT = b'silt$ '


def await_session(session):
    if not session.read_until(SESSION_PROMPT, timeout=120):
        raise TimeoutError('Silt session startup failed: ' + repr(session.output[-2500:]))
    if SESSION_MARKER not in session.output:
        raise AssertionError('missing session-script completion marker')
    if session.output.index(b'B3_INITD_READY: PASS') > session.output.index(SESSION_MARKER):
        raise AssertionError('session started before initd readiness')


def enter_recovery(session, runner):
    await_session(session)
    start = len(session.output)
    session.send('exit')
    if not session.read_until(runner.PROMPT, timeout=15, start_offset=start):
        raise TimeoutError('exiting /bin/sh did not restore nsh: ' + repr(session.output[-1500:]))
