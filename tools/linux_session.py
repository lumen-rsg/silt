"""Cleanup for a private Linux PTY test session, including background groups."""

import os
from pathlib import Path
import signal


def terminate_session(session_id):
    # Stop the test shell first so it cannot create new jobs during cleanup.
    # These fixtures' background bodies do not create additional descendants.
    try:
        os.kill(session_id, signal.SIGSTOP)
    except ProcessLookupError:
        pass
    terminated = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdecimal():
            continue
        pid = int(entry.name)
        descriptor = None
        try:
            if os.getsid(pid) != session_id:
                continue
            descriptor = os.pidfd_open(pid)
            # Recheck after pinning; the signal itself uses the stable pidfd.
            if os.getsid(pid) == session_id:
                signal.pidfd_send_signal(descriptor, signal.SIGKILL)
                terminated.append(pid)
        except ProcessLookupError:
            pass
        finally:
            if descriptor is not None:
                os.close(descriptor)
    return terminated
