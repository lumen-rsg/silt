# Signals and interactive terminal profile

Status: D4 bring-up; not full POSIX conformance or D4 acceptance closure.

## Signal mechanism

Silt uses Neva's action, mask, suspend, and signal-unwind syscalls. Querying an
action does not install it. Each caught action has a mask, augmented during
delivery by the signal itself. SIGKILL and SIGSTOP cannot be caught, ignored,
or blocked. Fork copies the calling thread's mask/dispositions and any active
handler frame, but not pending signals. Exec preserves the mask, pending
signals, and ignored dispositions while resetting caught handlers.

`sigsuspend` installs its temporary mask and enters the scheduler wait under
one lock. A handled signal completes it with EINTR and restores the original
mask. Default stop/continue does not complete that wait. Event and child waits
are interrupted only by deliverable signals; default-ignored SIGCHLD does not
interrupt them. Wait results distinguish normal exit 130 from death by SIGINT.

Service-backed terminal reads comprise multiple syscalls. Silt snapshots the
kernel's caught-signal delivery epoch at operation entry and supplies it to
the eventual Event wait. The scheduler rejects a changed epoch with EINTR
before consuming or blocking, closing the readiness-query/wait interruption
gap. Pipe waits use the same mechanism.

Dash releases its input-buffer interrupt guard around the blocking read. This
lets a SIGINT between its pending-flag check and libc operation entry escape
immediately, rather than leaving a deferred flag beside an input wait
that missed that signal. The deterministic Linux injection and SMP prompt stress are
recorded in [D4 resource recovery](d4-resource-recovery.md).

Caught signals do not detach an in-flight IPC call, receive or async wait from
its reply owner. Delivery remains pending until reply, timeout or peer teardown
completes that wait. Event readiness waits remain interruptible. Consequently an
infinite IPC wait on an unresponsive live peer has no bounded caught-signal
latency; cancellable IPC is not part of this profile. Valid handlers on RX image
VMAs can be registered before their instruction pages are resident.

The supported `sigaction` flags value is zero. Nonzero flags return ENOTSUP;
SA_RESTART, siginfo actions, alternate stacks, and nested handler delivery are
not implemented. `sigsuspend` from inside a handler is unsupported. `longjmp`
preserves AArch64 callee-saved SIMD registers and tells Neva when it abandons
the active signal frame. This is not a `sigsetjmp`/`siglongjmp` API.

Temporary handles in pipe readiness, terminal I/O/control and group queries now
use a cleanup stack checkpointed by setjmp and unwound by longjmp. Acquisition
and adoption are signal-masked to close the syscall-return ownership gap;
normal completion closes the same records. Handler-local jumps preserve older
records. Guarded handles are CLOEXEC, and forked chains remain independent.
Rebuild all static Silt images together: the 176-byte jmp_buf now uses its last
word for this checkpoint. This does not make arbitrary libc/application code
async-signal-safe. Neva's `docs/architecture/d4-interruption-cleanup.md` records
the bounded contract and exact capacity/interrupt stress tests.

Unhandled synchronous faults now carry a terminating signal through both Neva
wait interfaces: invalid memory references use SIGSEGV, undefined instructions
SIGILL, and alignment/allocation/pager failures SIGBUS. Ignored, blocked, or
nested synchronous faults terminate; a caught handler can exit or escape with
longjmp. Normal exit 139 remains distinct from death by SIGSEGV. Neva retains
its nonportable single-instruction skip on ordinary fault-handler return; this
is not general C recovery or a guarantee for invalid PC/SP. See Neva's
`docs/architecture/d4-fault-signal-status.md` for the exact classified syndromes,
native-versus-runtime boundary and test evidence.

## Process and terminal authority

POSIX pgids are group-leader PIDs, not Neva's separate group-object IDs.
Own-group discovery goes through the caller's sessiond catalog. Child group
handles are retained with the child record. `setpgid` acknowledges membership
already established while the child was suspended; arbitrary later regrouping
and new sessions are not supported. `killpg` uses a retained own/child group
capability, never an ambient numeric-group lookup.

Specific-child waits use retained Process capabilities. `waitpid(-1)` uses
Neva's existing direct-child wait-any selector, not a fallback from an invalid
capability or an arbitrary PID lookup. Silt still limits retained children to
16, subject to the smaller active Neva session-domain limits.

Terminal descriptors use ttyd reads/writes, with a freshly acquired caller
group capability on each request. `tcgetpgrp` reports the foreground leader;
`tcsetpgrp` routes UART handoff through sessiond with an inspect/signal group
capability; sessiond retains the exact owner for ttyd recovery. Foreground
handoff failure aborts still-suspended children rather than running them with
the wrong terminal ownership.

The termios subset supports ICANON, ECHO, ISIG, TOSTOP, and the erase, kill,
EOF, interrupt, and suspend characters, with TCSANOW. Other flag families and
attribute-change actions return ENOTSUP. Canonical input ends at newline/EOF;
raw input returns available bytes without canonical editing. Background reads
stop with SIGTTIN and resume after `fg`; ignored/blocked SIGTTIN returns EIO.
Default-action TOSTOP writes stop with SIGTTOU. Attribute mutation and UART
foreground handoff apply the same rule regardless of TOSTOP. Ignoring or
blocking SIGTTOU permits the operation without generating a signal for the
caller or peers. Caught SIGTTOU returns EINTR without mutation; default
stop/continue retries, stopping again if resumed in the background and
completing after `fg`. ttyd/sessiond enforce policy using authenticated caller
membership and Neva's atomic conditional group-signal mechanism, not libc claims.
Terminal writes stage read-only sources in writable service payloads so reply
copyback does not falsely fail after output. See Neva's
`docs/architecture/d4-background-terminal-policy.md` for the runtime evidence.

Terminal lifecycle observation in sessiond uses Neva's NONBLOCK|PEEK Event wait.
It can observe sticky restart completion without consuming ttyd's counting
readiness notification between a reader's WOULD_BLOCK result and its wait.
Ordinary readers still consume tokens; this is not a general broadcast protocol
for competing PTY clients. Neva's `docs/architecture/d4-terminal-observer-wakeup.md`
records the reproduced timeout, source defect, regression gates and the remaining
causal uncertainty: the original failing snapshot did not include input queues.

## Anonymous pipes

Pipes use Neva's capability-backed byte stream, not shared-memory libc counters.
Last-writer death drains buffered bytes then returns EOF; last-reader death
wakes writers with SIGPIPE/EPIPE. Fork/dup/exec and FD_CLOEXEC use endpoint handle
lifetime, including death before a suspended child runs. The private Silt exec
handoff is now version 2; rebuild all Silt images together.

The buffer holds 4096 bytes and PIPE_BUF is 512 bytes. Writes up to PIPE_BUF
are atomic; larger writes are split. Readiness uses broadcast one-shot Events
with subscribe/retry/wait ordering and caught-signal epochs. O_NONBLOCK via
F_SETFL explicitly returns ENOTSUP. See Neva's
`docs/architecture/d4-byte-stream-lifetime.md` for implementation and evidence.

## Remaining D4 acceptance work

- The captured SMP reaper lock inversion is fixed with a deterministic test.
  Historical filesystem-provider parse failures remain unattributed; see the
  port plan for exact completed matrices rather than assuming general closure.
- Active foreground ownership now survives separate ttyd/sessiond crashes.
  `tools/test_rootfs.py --restart-tests` checks the same blocked foreground
  reader, stopped background jobs and Ctrl-C after repeated service replacement.
  Simultaneous service loss, termios/queue persistence and in-flight transaction
  recovery are not covered by this narrower ownership-continuity slice. See
  Neva's `docs/architecture/d4-active-job-recovery.md`.
- Uncatchable-death pipe lifetime is implemented with a dedicated runtime gate;
  temporary pipe/TTY handles are also reclaimed after handler longjmp. Shared
  open-description status flags, nonblocking pipe I/O and interruption of other
  libc construction paths still need a broader POSIX descriptor pass.
- Synchronous fault status is implemented, including dash diagnostics,
  background wait, pipeline and EXIT-trap status checks. Core dumps, siginfo,
  alternate stacks and a complete architecture fault personality remain absent.
- Background mutation and ignored/blocked SIGTTOU policy are implemented for
  the UART controlling-terminal profile. Orphaned-group EIO rules and full
  POSIX controlling-terminal acquisition/PTY sessions remain open.
- Twenty shared Linux/Silt job/trap cases now cover retained wait status,
  substitution, traps across exec, job selection and reaping. Their
  [acceptance record](d4-job-trap-conformance.md) distinguishes upstream-derived
  cases, a pinned-Dash wait-all discrepancy, and the current verification state.
  Fourteen additional [wait-interruption checks](d4-wait-interruption.md) cover
  caught SIGINT in observed suspended waits and retained-child reaping. Twenty
  [foreground-pipeline checks](d4-foreground-pipelines.md) cover two-member
  stop/continue, per-member INT/EXIT ordering, last-member status and the shell's
  foreground SIGINT trap. Sixty [resource-recovery checks](d4-resource-recovery.md)
  cover process-quota refusal, partial pipeline cleanup, terminal recovery and
  existing-job preservation. Broader signal-arrival cases and other resource
  failures remain open. The bounded libc interruption-cleanup evidence remains in Neva's
  `docs/architecture/d4-interruption-cleanup.md`.
- Subsequent pager retry and lifetime-owned ASID corrections passed consecutive
  SMP-8 matrices; see Neva's `d4-smp-retest-2026-09-13.md` and
  `d4-asid-lifetime.md`. These supersede the earlier incomplete matrix status,
  without attributing every historical fork-epilogue or provider failure.

The Linux PTY interrupt regression and shared job/trap cases are targeted
checks against the prepared upstream-source reference, not an imported upstream
conformance suite. The QEMU rootfs suite is curated Silt acceptance. Neither
certifies complete shell or OS POSIX compatibility.
