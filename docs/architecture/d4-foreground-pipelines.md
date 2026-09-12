# D4: foreground pipeline stop, continuation and traps

## Contract and scope, 2026-09-13

Twenty new checks run identical shell commands on the pinned Linux Dash
0.5.13.5 build and Silt. They exercise two-member foreground jobs, terminal
Ctrl-Z, background and foreground continuation, process-group SIGSTOP, per-member
INT/EXIT traps, last-member status, and foreground SIGINT propagation to the
interactive shell. Together with the preceding job/trap and wait-interruption
suites, the shared reference now contains 54 checks.

This item changes the test drivers and evidence only. It requires no kernel,
libc, Dash patch or rootfs change. It does not claim full POSIX job-control or
signal conformance.

## Test protocol

`tools/dash_pipeline_cases.py` owns the commands and assertions. Three cycles
use distinct left/right exit statuses: 17/23, 31/7 and 5/0. Each cycle requires:

1. Both pipeline members announce readiness from their function bodies before
   the harness sends terminal Ctrl-Z. Dash returns the symbolic TSTP status.
2. `jobs -l` reports a stopped job and lists both stage commands. Dash prints
   the job's status once, not separately for each listed process.
3. `bg %+` succeeds and both members acknowledge SIGCONT through `/dev/tty`.
4. `kill -STOP %+` stops the group again, observed through `jobs -l`.
5. `fg %+` makes both members acknowledge continuation before terminal Ctrl-C.
   Each member's INT trap reports its identity and exits with its chosen status.
   Its EXIT trap observes that status and ends with `false`. Each member must
   report CONT, then INT, then EXIT, before the shell reports the pipeline's
   final status. The shell must return the rightmost member's status, including
   zero when the left member failed. No ordering between separate members is
   assumed.
6. The completed job is absent from `jobs`.

The final two checks use a fresh pipeline with default INT dispositions and a
caught INT in its parent shell. After both children announce readiness, terminal
Ctrl-C terminates the foreground job. Dash runs its shell INT trap before the
next command; both see status 130 even though the trap ends with `false`. The
job must then be fully reaped. This covers Dash's foreground-job SIGINT
propagation: the pinned `src/jobs.c` sets `job->sigint` from signal-death status
and `waitforjob` raises SIGINT in the waiting shell. It does **not** inject an
independent signal directly into the parent while its foreground job remains
running, so general foreground-command trap deferral remains separate coverage.

Each run delivers three terminal stops, three explicit group stops, six
continuations and four terminal interrupts in this new suite. Every terminal
stop or interrupt waits for both stage acknowledgements. Child identity and
requested exit status live in variables private to each forked stage, so traps
remain correct when they interrupt nested shell functions.

The shared phase driver waits for every marker from one phase before sending
its input, accepts either member's arrival order, and advances beyond both
markers before the next phase. The transports exclude command echoes and use
fresh command-scoped offsets, completion markers and prompt synchronization.
The Linux transport includes late background acknowledgements after its prompt
in the returned output. Native regressions reject a missing second member and
prevent reuse of old phase markers. All input retains the existing 128-byte
framed canonical-line limit.

## Fixture failures and controls

The first Linux attempt failed because `^Z` echoed on the same line as the
stop-status marker. The fixture now saves the status before emitting a leading
blank line. The second expected two occurrences of `Stopped`; source inspection
confirmed that Dash prints the job status once. The third used function
arguments in EXIT traps, but Dash unwinds those arguments before executing EXIT
handlers. These attempts remain in `build/d4-pipeline-linux-v1.*` through `v3.*`.

A later repeated reference run exposed a second argument-lifetime problem:
SIGSTOP could arrive while the previous CONT trap was still in its printing
function. On foreground continuation, the next CONT trap used that function's
`$1` and emitted `PT_CONT_CONT=yes` rather than the stage identity. This is a
fixture race, observed on Linux, not a Neva SMP failure. The failed frozen
attempt remains in `build/d4-pipeline-preready-race/`, including run 4's exact
transcript. All signal handlers now use retained per-child identity/status
variables. The corrected suite passed 64 consecutive Linux stress runs in
`build/d4-pipeline-linux-stress/` before final qualification.

An isolated Linux Dash copy changes `getstatus` to select the first pipeline
member instead of the last. Running the new suite alone rejects cycle 1 after
both correct EXIT reports, because Dash returns 17 instead of 23. The final
control uses the same shared cases as acceptance and retains its build log,
`reference-final.log` and PTY transcript in `build/d4-pipeline-negative/`.
A second isolated copy suppresses `waitforjob`'s `raise(SIGINT)`. Its first 18
pipeline checks pass, but the shell-trap check rejects the missing trap output
before status 130. This control retains `build/d4-pipeline-signal-negative/`.
The original vendor tree and reference binary are unchanged.

The initial SMP-8 target attempt passed 163/163 checks using the earlier
18-check version, one terminal cycle and no service crashes. It is retained in
`build/d4-pipeline-target-v1.*`; it is not part of the final matrix.

## Verification

All five Silt host/reference gates pass. The final frozen Linux reference
passed sixteen consecutive runs of all 54 shared checks, following the 64-run
fixture stress test. Both negative controls were rejected at their intended
assertions. The frozen target matrix passed **185/185 on 1/4/8/8/8/8 CPUs**,
including four consecutive SMP-8 runs. Across the matrix, all 120 new pipeline
checks pass alongside 54 observed wait interruptions, 24 terminal cycles and
24 injected service crashes.

| Run | Passed | Failed | Seconds |
|---|---:|---:|---:|
| rootfs-1-smp1 | 185 | 0 | 61.57 |
| rootfs-2-smp4 | 185 | 0 | 88.56 |
| rootfs-3-smp8 | 185 | 0 | 173.59 |
| rootfs-4-smp8 | 185 | 0 | 184.30 |
| rootfs-5-smp8 | 185 | 0 | 189.10 |
| rootfs-6-smp8 | 185 | 0 | 179.98 |

`verify.py` checked every frozen input against `sha256.json`, all current test
scripts, the current kernel/rootfs/reference binary, and each run's kernel/ttyd
debugger images. Every hash matches. It also checked all per-run return codes,
case counts, wait observations, terminal cycles and crash injections;
`verified-results.md` retains those results.

Exact inputs, hashes, per-run kernel/ttyd debugger images, UART/PTY transcripts,
commands, return codes and durations are retained in `build/d4-pipeline-final/`.
The rootfs runner uses fresh private writable media for each run. Existing
wait-interruption cases still require read-only GDB observation before signal
delivery; the new pipeline cases synchronize through ordinary terminal output.

| Artifact | SHA-256 |
|---|---|
| Neva kernel | `558041de8305ccb37d2890d845a4d2e529400458661430c39610ade319a6dd6e` |
| Silt rootfs | `7c272db0aaae4d9107428e332892e0ddd9034bbebcf4da7af367fc5722a4551a` |
| Linux Dash | `e4b56cd9c33803f180f3e746a20299802c96f1bac9da9ccf5af77717216b24a0` |
| Shared pipeline cases | `00a704dc956f952a69d42924124f27c7944dd018260723ea6a453572b13a803b` |

The kernel and rootfs remain the Neva `227fcce` and Silt `0e00708` artifacts.
Earlier normal/VM-failure kernel matrices remain earlier evidence on those same
bytes, not newly executed kernel gates. These QEMU results do not qualify
physical ARM hardware or attribute every historical SMP/provider failure.

Run the shared reference with:

```sh
meson test -C build dash-linux-job-trap-reference --print-errorlogs
```

Run the complete target gate from Silt with:

```sh
python3 tools/test_rootfs.py --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson --rootfs build/silt-rootfs.img \
  --smp 8 --terminal-cycles 4 --restart-tests \
  --uart-log build/pipeline-run.uart.log --gdb-log build/pipeline-run.gdb.log
```

## Remaining work

The next D4 item is resource-exhaustion behavior and recovery, including failed
process/pipeline construction within the session quota and retained-resource
cleanup. Signal-zero probing, independently delivered foreground-shell signals,
other signal-arrival windows/dispositions, orphaned-group EIO and full
controlling-terminal/PTY session semantics remain open. D4 remains open.
