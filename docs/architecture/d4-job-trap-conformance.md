# D4: shared Dash job and trap acceptance

## Scope, 2026-09-13

Twenty original cases now run unchanged against the pinned Dash 0.5.13.5 Linux
build through a controlling PTY and against Silt Dash through Neva's UART.
The Linux build uses the prepared Silt patch set with host no-op pipeline hooks.
`tools/dash_job_cases.py` owns the commands and exact output-line expectations;
`tools/test_dash_jobs.py` supplies the Linux transport, and `tools/test_rootfs.py`
executes the same cases inside the complete rootfs acceptance run. Meson includes
the Linux reference gate. This is a bounded compatibility slice, not full D4,
POSIX shell or OS certification. No kernel, libc, Dash patch, or image change
was required by these cases.

The pinned release archive has no bundled job-test suite. Two cases are original
regressions derived from upstream fixes, rather than copied upstream tests:
Herbert Xu's [jobs-only command substitution fix](https://www.spinics.net/lists/dash/msg02539.html)
and [trap-only command substitution fix](https://www.spinics.net/lists/dash/msg02538.html),
both dated 2024-04-15 and present in the pinned source. The remaining cases use
Dash's actual Linux behavior together with the supported signal/terminal profile.
The [POSIX.1-2017 wait description](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/wait.html)
provides the status and operand contract; the reference discrepancy below remains
explicit.

## Covered behavior

| Area | Cases | Required observation |
|---|---:|---|
| Child wait and asynchronous pipelines | 5 | Saved statuses survive reverse wait order; multiple operands return the final status; wait-all returns zero; unknown PID returns 127; pipeline status comes from its last member |
| Trap status | 3 | A caught action preserves the interrupted status; EXIT sees and preserves the original exit status; explicit exit inside EXIT replaces it |
| Trap text and inheritance | 5 | Trap-only substitution retains parent text; ordinary substitution clears caught traps; saved traps can be restored; exec retains ignored signals and resets caught signals |
| Job tables and selection | 5 | Jobs-only substitution retains both parent jobs; ordinary substitution clears them; current/previous selectors stop both groups; prefix/substring selectors resume them |
| Reaping | 2 | Both signaled children report termination status; no jobs remain after reaping |

The two live jobs use distinct function names and unconditional SIGSTOP.
Bounded `jobs` polling observes both stopped states before continuation. The
suite does not assume the order of job listings. It retains exact child PIDs
for cleanup, and checks exact output lines after the echoed command. No new
executable consumes a filesystem VMO binding, and the workload fits the existing
eight-process session quota.

Signal numbers are platform ABI values: SIGUSR1 is 10 on this Linux host and
16 on Neva. The exec-reset case checks the symbolic name from `kill -l` applied
to the observed exit status. It does not incorrectly require Linux's numeric
exit code on Silt.

## Reference discrepancy and failed fixture attempts

The first Linux test expected an immediate `wait $a` after operand-free `wait`
to return 127. The pinned Dash instead retains status 9 until later job cleanup;
Silt matches that result. The source marks jobs waited without freeing the
records inside `waitcmd`. The test explicitly names this a pinned-Dash retained
status check. It is not evidence for the cited POSIX.1-2017 wording that wait-all discards
statuses. The failed expectation is retained in `build/d4-job-reference-v1.log`.

A second Linux attempt assumed one ordering of two `jobs` lines. The corrected
case accepts either order while requiring both names. The failed attempt is
`build/d4-job-reference-v2.log`.

The first target attempt passed eleven cases, then truncated a 133-byte command
at ttyd's 128-byte canonical edit limit, leaving Dash at its continuation prompt.
The UART and read-only snapshot are retained as `build/d4-job-v1-smp1.*`. Commands
were shortened without changing their semantics. The shared `frame_command`
now rejects lines exceeding 128 encoded bytes, including the completion marker,
and rejects embedded line delimiters before sending anything. A host regression
covers the exact boundary. The target driver also requires prompt return after
its completion marker. No terminal limit or deadline was increased.

The next target attempt exposed the incorrect Linux SIGUSR1-number assumption:
Silt correctly reported 144 instead of 138. Logs are `build/d4-job-v2-smp1.*`.
After symbolic decoding, the pre-final one-CPU run passed all 131 checks without
service injection (`build/d4-job-v3-smp1.*`). None of these failed attempts is
counted as a clean final run.

## Negative controls

An isolated copy of the pinned Linux source in `build/d4-job-negative/dash/`
was rebuilt after removing the `issimplecmd(n, JOBSCMD->name)` early return from
`forkchild`. Its first thirteen shared cases pass, then the jobs-only substitution
case fails because the parent jobs were cleared. The unmodified reference passes.
`build/d4-job-negative/{build.log,reference.log,reference.pty.log}` retain the
build and rejected observation.

A second isolated copy in `build/d4-trap-negative/dash/` forces `simplecmd = 0`
in `clear_traps`. Its first eight cases pass, then trap-only substitution fails
because the parent trap text is absent. The corresponding build, result and PTY
logs are retained in `build/d4-trap-negative/`. Both controls were rebuilt
independently from the original prepared tree; the vendor source and shipped
binary are intact.

## Verification

The five Silt host/reference tests pass. The final Linux reference passed sixteen
consecutive repetitions of all twenty shared cases. Full PTY transcripts are
retained in `build/d4-job-final/linux-{1..16}.pty.log` alongside the result logs.
The frozen QEMU matrix passed **151/151 on 1/4/8/8/8/8 CPUs**, including four
consecutive eight-CPU runs. Each run executes all twenty shared cases, four
terminal stress cycles and four injected service crashes: 120 shared-case
executions, 24 terminal cycles and 24 crashes overall. The existing ASID,
cleanup, pipe, signal, fault and terminal checks remain in the integrated run.

| Run | Passed | Failed | Seconds |
|---|---:|---:|---:|
| rootfs-1-smp1 | 151 | 0 | 52.32 |
| rootfs-2-smp4 | 151 | 0 | 58.23 |
| rootfs-3-smp8 | 151 | 0 | 143.71 |
| rootfs-4-smp8 | 151 | 0 | 127.27 |
| rootfs-5-smp8 | 151 | 0 | 138.54 |
| rootfs-6-smp8 | 151 | 0 | 142.39 |

`build/d4-job-final/results.json` records exact executed commands, exit codes
and durations; `rootfs-*.log` and `rootfs-*.uart.log` retain results and UART.
`verified-results.md` records the final hash/count verification. All frozen
inputs and each run's kernel/ttyd debugger images match their recorded hashes.
The current source scripts, kernel, rootfs and Linux reference binary match the
tested copies.

| Artifact | SHA-256 |
|---|---|
| Neva kernel | `558041de8305ccb37d2890d845a4d2e529400458661430c39610ade319a6dd6e` |
| Silt rootfs | `7c272db0aaae4d9107428e332892e0ddd9034bbebcf4da7af367fc5722a4551a` |
| Linux Dash reference | `e4b56cd9c33803f180f3e746a20299802c96f1bac9da9ccf5af77717216b24a0` |
| Shared case module | `8b39606da60f47a5104cd1d31d2383ef85d893c6a09285b0fbab53869d97142f` |

The executed rootfs command shape, from the Silt checkout, is:

```sh
python3 tools/test_rootfs.py --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson --rootfs build/silt-rootfs.img \
  --smp 8 --terminal-cycles 4 --restart-tests \
  --uart-log build/job-run.uart.log --gdb-log build/job-run.gdb.log
```

The Linux gate is `meson test -C build dash-linux-job-trap-reference`; the
standalone driver also accepts a second argument for the full PTY transcript.

The kernel and rootfs are unchanged from Neva `227fcce` and Silt `0e00708`.
This is new execution evidence for the shared cases on those artifacts; the
normal/VM-failure matrices and native ASID protocol coverage remain the previously
recorded evidence in Neva's `docs/architecture/d4-asid-lifetime.md`, not new runs.
All media are copied privately by the canonical QEMU runner. Frozen inputs,
SHA-256 records, commands, timings and per-run debugger symbols are in
`build/d4-job-final/`.

## Remaining acceptance

D4 remains open. Next are caught-signal interruption of Dash's `wait` builtin,
multi-member foreground pipeline stop/continue and trap ordering, and bounded
job-table/resource-exhaustion behavior. The existing rootfs gates cover terminal
interrupts and job foreground recovery, but these twenty cases do not extend
those to every wait/trap interleaving. Orphaned-group EIO and full controlling
terminal/PTY session semantics remain outside the current UART profile. Historical
unattributed failures remain recorded in the port plan. Qualification here is
Linux reference plus QEMU, with no physical ARM or full POSIX claim.


The subsequent [caught-wait acceptance slice](d4-wait-interruption.md) adds
fourteen shared checks, with explicit observation before SIGINT delivery and
retained-child reaping. Its record supersedes the interrupted-wait next-item
status above for that bounded case; broader signal and pipeline coverage remains
open.
