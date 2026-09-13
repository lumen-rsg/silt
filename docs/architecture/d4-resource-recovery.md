# D4: process quota rejection and pipeline recovery

## Contract and scope, 2026-09-13

Dash now recovers from failed process and pipeline admission without losing its
foreground terminal, retaining partial jobs, or leaking the pipeline's local
file descriptors. Silt's session domain remains limited to eight processes.
The test runs recovery `nsh` plus interactive Dash in that domain, leaving six
slots for Dash's children. Neither the quota nor kernel policy is relaxed.

This change updates Silt libc, the maintained Dash patchset, an existing native
fixture and the acceptance drivers. The Neva kernel is unchanged. The bounded
acceptance covers process-quota refusal, suspended pipeline rollback, existing
job preservation, descriptor recovery and subsequent successful pipelines.
It is not a complete resource-exhaustion or POSIX qualification.

## Reproduced defects and implementation

With five ready background children, one process slot remains. The old Silt
build admits the first pipeline stage, gives its group the terminal, then fails
the next fork. Although the suspended stage is terminated, Dash's next input
read occurs from a background group. The reproduction prints `[1] stopped dash`
and returns to `nsh`; the intended Dash status command never completes.
`build/d4-resource-before.*` retains the UART and read-only snapshot, confirming
Dash PID 42 is THREAD_STOPPED. The source's premature handoff and subsequent
background-read path are consistent with SIGTTIN. The snapshot's ttyd
`foreground_group` field is an opaque local capability, not a POSIX group ID.

`libc/process.c` now remembers a pipeline's foreground intent and delays the
terminal handoff until all stages have been admitted, immediately before they
are resumed. Abort and child-fork reset clear that intent and pending job state.
Single-process foreground jobs retain their existing handoff path.

Dash's `evalpipe` also owned descriptors in local variables across a `forkshell`
error jump. The old Linux reference demonstrates the leak: its descriptor count
rises from **4 to 12** after four rejected pipeline constructions with no slots
allowed. `build/d4-resource-negative-final.*` retains that rejected control.
The control uses the previous prepared Dash binary and the current test suite.

The new `0004-unwind-pipeline-construction.patch` installs a construction-only
exception handler in `evalpipe`. It tracks the preceding read end and pending
pipe ends in volatile locals, closes them on an error, restores the outer
handler and rethrows the original exception. Child execution restores its own
outer handler before evaluating the stage; successful construction removes the
guard before waiting for the job.

A shared `abortjob` path cancels the Silt suspended transaction, terminates and
reaps recorded partial children, frees the job record and restores the root
shell's foreground terminal. On Linux, stages can run before the pipeline is
complete, so reaping precedes terminal restoration: a late child must not take
the terminal back after the shell recovers. Existing unrelated jobs are not
selected for cleanup. The added direct `waitpid` dependency is recorded in the
Dash required-symbol contract. Source preparation advances from patchset 8 to 10, including the input-race
correction found during the final gate below.

## Runtime assertions

`tools/dash_resource_cases.py` supplies 60 shared checks. It fills the session
with ready background jobs, leaving zero, one, three or five available slots.
For each case, the target really reaches the existing domain process quota.
Child readiness is required before later SIGTERM cleanup, avoiding inherited
interactive-shell signal dispositions during fork initialization.

With one, three or five slots available, the suite twice rejects pipelines at
the final stage and at an intermediate stage. With zero slots it repeatedly
rejects the first stage of a two-member pipeline, a subshell, an external Dash
launch and a background subshell. This totals 22 failed admissions per run.
Each failure must produce `Cannot fork`, return to the same interactive prompt,
leave status 2, preserve the existing running jobs and remove the partial job.
On Silt, no admitted stage may print its body marker: those stages must remain
suspended until construction commits.

After every capacity scenario, all existing children must terminate and reap,
the descriptor count must match its baseline, a fresh producer/consumer
pipeline must succeed, and `jobs` must be empty. Linux counts the shell's actual
`/proc/PID/fd` entries while it is at its prompt. Silt executes the existing
`check-cleanup fds` fixture and counts inherited descriptors across exec; that
probe detects the non-CLOEXEC pipe descriptors implicated in this defect. It
does not measure arbitrary CLOEXEC leaks or every kernel object globally.

The native `check-cleanup quota` mode supplies separate libc evidence. Four
rounds each create six children, require eight more forks to fail with EAGAIN,
kill and reap the six children with SIGKILL status, and require the parent
capability-table capacity to equal its baseline. The next round must refill all
six slots. This covers 32 quota refusals and 24 successful child lifetimes per
run. It reuses an existing executable binding instead of adding another image
to the FSD executable cache.

## Linux reference and harness boundaries

Linux uses the same shell commands with an explicit `LD_PRELOAD` fork injector,
not a claimed reproduction of Neva's domain accounting. A private control file
selects a generation and successful-fork budget for each rejected command.
The injector returns EAGAIN at that boundary and emits `RX_INJECTED_FORK`;
the harness rejects failures without this evidence. Its `vfork` path uses COW
fork, matching Silt's adapter. Only the test shell receives the preload and a
32-descriptor RLIMIT_NOFILE; account-wide process limits are not changed.

Linux prefix stages can execute before rejection. The reference therefore
compares status, terminal/job recovery and descriptor cleanup, while the
no-stage-execution assertion is specific to Silt's suspended construction.
Rejected commands intentionally skip their appended completion marker; the
transport instead requires the diagnostic and prompt before querying status.
Command echoes are excluded and framed input remains within 128 bytes.

The first Linux recovery implementation closed descriptors but could still
lose the terminal after partial construction (`build/d4-resource-linux-v1.*`).
The final abort path also reaps running prefix stages before terminal recovery;
`v2.*` passes all 60 checks. An early native fixture build accidentally called
an unavailable `printf`; it now uses the existing native output helpers.
The initial symbol audit also caught the new `waitpid` dependency before its
contract was updated. These failed development checks are not acceptance runs.

Broader parallel reference runs exposed the previous job tests' fixed 16-poll
limit: they could still report Running before the host delivered/observed all
stops. Both shared job suites now use `poll_command` with a five-second polling budget
and a short interval, retaining the same stopped-state assertions and per-command
transport deadlines. Failures are
retained in `build/d4-resource-host-final.log` and
`build/d4-resource-reference-stress-v2.log`.

Reference stress also rejected a fork failure whose injection marker was
missing (`build/d4-resource-reference-stress.log` and
`build/d4-resource-injector-stress.log`). The original injector did not retry
interrupted control I/O or marker writes. Those operations now retry EINTR;
the exact interrupted syscall was not captured in the failed run. The corrected
job and resource suites passed 32 runs each in parallel, retained in
`build/d4-resource-reference-stress-v3.log`.

## Host cleanup and the stopped matrix

The first full matrix attempt timed out after 180 seconds in the existing
background terminal cleanup fixture, before the new resource cases. Its UART,
result and snapshot remain in `build/d4-resource-precleanup-timeout/`. The
snapshot shows the child running in the handle-capacity loop, with the parent
waiting; it does not establish a kernel deadlock.

The host still had seven busy-loop children from failed Linux reference runs,
each consuming about one CPU, plus two stopped test children. They survived
because cleanup killed only the shell, while job control placed the background
children in separate process groups. Exact PID/session/control-file identities
were retained in `build/d4-resource-host-orphans.json`; only those verified
current-test processes were removed. The identical pre-input-fix kernel/rootfs then
passed **247/247 on one CPU**, including the previously timed-out fixture, in
`build/d4-resource-exact-retest.*`. This supports host CPU contention as the
cause of the timeout; the failed run is retained and excluded from acceptance.

`tools/linux_session.py` now stops the test shell and uses session membership
plus pidfds to terminate its remaining jobs across process groups. Cleanup also
runs if writing the transcript fails. A native host regression creates a
separate background group in a private session and verifies both processes are
terminated while the test runner is untouched. This requires Linux `/proc` and
Python's pidfd APIs. Successful reference runs retain `LINUX_SESSION_CLEANUP`
records. The final matrix restarts from fresh media with this corrected harness.

## Prompt-interrupt race found during the gate

The first frozen six-run matrix (`build/d4-resource-final/`) passed 247/247 on
1, 4, 8, 8 and 8 CPUs. Its sixth run passed all 60 resource assertions and the
native quota fixture, then stopped at repeated prompt interrupt 35. That matrix
is retained as a failed gate, not acceptance of the final artifact.

A focused old-artifact probe reproduced the missing prompt at interrupt 4,109
(`build/d4-lock-diagnosis/run3.*`) and again after 1,100 interrupts (`run4.*`).
The prompt remained absent during another 60 seconds of observation. Read-only
snapshots show advancing per-CPU timer counts and ttyd deadlines; the apparent
scheduler-lock stall in a single snapshot was not a kernel deadlock. Dash's
`intpending=1`, `suppressint=1`, normal output flags and input wait identify the
actual failure: SIGINT arrived after `preadfd` checked `int_pending()`, but
before Silt's read sampled the delivery epoch. The handler deferred the interrupt
in Dash, while the read treated the already-delivered signal as its baseline
and blocked waiting for new input.

Patch `0005-enable-interrupts-during-input-read.patch` releases the input-buffer
interrupt guard around the blocking `read` and restores it before processing
returned bytes. Buffer metadata is already consistent at that point. The normal
interactive read can therefore escape immediately on SIGINT, including that
check/read window; enclosing interrupt guards retain their nesting. Silt's
existing longjmp cleanup releases the read's temporary capabilities. This is a
Dash change with no new kernel or libc ABI. Both prepared builds disable tee
and libedit; this gate does not qualify those other upstream input backends.

`dash-linux-read-signal-reference` preloads a private Linux read interceptor
which raises SIGINT exactly at read entry. It requires an injection marker,
a new prompt and a subsequent command for each of 16 cycles. The old patchset-9
binary fails at the first injected signal; patchset 10 passes. These controls
are retained as `read-negative.*` and `read-fixed.*` in the diagnosis directory.
A 10,000-interrupt Linux run without injection passed on the old binary,
demonstrating why the forced boundary is needed alongside random timing stress.
The final target matrix uses 10,000 prompt interrupts on its first SMP-8 run
and the existing 160 on the other runs, without extending acceptance deadlines.

A separate diagnostic command-echo failure (`run1.*`) coincided with host
suspend from 12:59:52 to 13:10:13 local time on 2026-09-13, confirmed by the
systemd suspend journal retained in `host-suspend.log`; its timeout was recorded immediately after resume.
It is excluded from runtime acceptance. The later focused prompt failures did
not coincide with that suspension. `run2.*` passed the full gate with 1,600
prompt interrupts but did not reproduce the race; it does not override the
subsequent failures or the deterministic negative control.

## Verification

The final frozen matrix passed on the exact artifacts below:

| Run | Passed | Failed | Seconds | Prompt interrupts |
|---|---:|---:|---:|---:|
| rootfs-1-smp1 | 247 | 0 | 101.01 | 160 |
| rootfs-2-smp4 | 247 | 0 | 77.86 | 160 |
| rootfs-3-smp8 | 247 | 0 | 331.55 | 10000 |
| rootfs-4-smp8 | 247 | 0 | 189.37 | 160 |
| rootfs-5-smp8 | 247 | 0 | 207.13 | 160 |
| rootfs-6-smp8 | 247 | 0 | 215.11 | 160 |

All frozen inputs, current sources/scripts and per-run symbols match. The
matrix includes 54 observed wait interrupts, 24 terminal cycles and 24 service
crashes. Sixteen frozen Linux runs pass each 54-check job/trap, 60-check resource
and 16-injection read-signal suite. All seven host gates pass.

All 48 Linux sessions reported cleanup and had no remaining session members
in the independent `/proc` check. The full matrix contains 360 shared resource
checks and six native quota/refill runs. The earlier failed matrices and
negative controls remain retained with their diagnoses; these results do not
relabel those failures as passes.

The focused SMP-8 target run passed 67/67 checks: six boot checks, the native
quota fixture and all 60 shared resource checks. Its evidence is retained in
`build/d4-resource-after-v2.*`. This is preliminary evidence, separate from the
full final matrix and its complete lifecycle/terminal/service-crash workload.

`build/d4-resource-accepted/` retains frozen runtime artifacts, test tools, changed
implementation sources, hashes, Linux PTY and target UART transcripts, per-run
kernel/ttyd debugger images, commands, return codes and durations. Every QEMU
run uses fresh private writable media. Existing wait cases continue to use
read-only GDB observation; process-quota refusals use normal runtime admission.

Run the build and all seven host/reference gates with:

```sh
meson compile -C build
meson test -C build --print-errorlogs
```

Run the complete target gate from Silt with:

```sh
python3 tools/test_rootfs.py --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson --rootfs build/silt-rootfs.img \
  --smp 8 --terminal-cycles 4 --prompt-interrupts 10000 --restart-tests \
  --uart-log build/resource-run.uart.log --gdb-log build/resource-run.gdb.log
```

An existing older vendor directory must be moved aside before preparing
patchset 10: `tools/prepare_dash.sh` deliberately refuses to overwrite an
existing source tree. This workspace retains the older trees for the negative controls.

| Artifact | SHA-256 |
|---|---|
| Neva kernel | `558041de8305ccb37d2890d845a4d2e529400458661430c39610ade319a6dd6e` |
| Silt rootfs | `5be69d7f6948fde3d6f6484b348512d3720f35299dd274c6dfdcb428004ccb7a` |
| Silt Dash | `f04c4a5aee3adca8be65e4e3878a8b3caffedff00bfcca019b2d3e9f55f8e90a` |
| Linux Dash | `2d6f120e81471ae113dfa45aa02c5aa479e48077601ff7013305b63f235cf62e` |
| Shared resource cases | `f594aa7b4eafa98dde5dd13c8e275d5697c934ae3c3666ebf2d1a20207032ec6` |

## Remaining work

D4 remains open. Descriptor/pipe allocation refusal and address-space or
capability exhaustion during other construction phases need separate targeted
acceptance, including failure during terminal transfer or stage resume. This
record does not claim those phases were fault-injected merely because they
share cleanup code. Signal-zero probing, independently delivered signals to a
shell with a running foreground job, orphaned-group EIO and full terminal/PTY
session semantics remain open. Physical ARM qualification is separate from
these QEMU results.
