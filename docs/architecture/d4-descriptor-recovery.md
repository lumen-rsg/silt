# D4: descriptor exhaustion and redirection recovery

## Contract and implementation, 2026-09-13

Silt refuses descriptor-table exhaustion before `open` creates or truncates a
file and before `pipe` requests kernel endpoints. `F_DUPFD` and
`F_DUPFD_CLOEXEC` reject an out-of-range minimum with EINVAL; a valid minimum
with no available descriptor reports EMFILE. The existing per-process bound
remains 32 descriptors. `strerror` now names EMFILE and ENFILE rather than
printing "unknown error". The Neva kernel and its limits are unchanged.

The native negative control filled the table with aliases and called
`open(existing, O_WRONLY | O_TRUNC)`. The call failed with EMFILE, but the file
had already been truncated: the retained descriptor could no longer read its
original four bytes. `build/d4-descriptor-before.uart.log` records failure 103
and exit status 103 with the pre-fix libc. The corrected fixture verifies both
`open` and `openat`, preserves the existing contents, and requires a refused
O_CREAT path to remain absent. Admission checks address an already-full table;
this is not qualification of signal-handler reentrancy during filesystem RPCs.

The Linux reference exposed two related Dash redirection failures:

- A failed attempt to save a redirection target called `savefd(fd, fd)`, which
  closed the original descriptor even when duplication failed. A newly opened
  redirection source also remained unowned across the resulting exception.
- `exec 3>&-` tried to save fd 3 before permanently closing it. With no spare
  descriptor, the shell reported an error instead of successfully releasing it.

Maintained patch `0006-recover-exhausted-redirections.patch` saves a target
without closing it until duplication succeeds. A local exception handler
closes only a newly opened source on failure; borrowed `>&` sources remain
owned by the caller. Closing-only, no-argument `exec` commands without variable
assignments skip the rollback copy, while retaining Dash's redirection-stack
and closed-descriptor bookkeeping. Other redirections keep their normal
save/restore behavior. Source preparation advances to patchset 11 and applies
the patch to a fresh verified upstream archive for both Linux and Silt builds.

No change to the pipeline abort code was necessary: the previous construction
exception guard also reclaims partial jobs when `pipe`, rather than `fork`,
fails. The new tests explicitly exercise that boundary.

## Runtime assertions and reference boundary

`check-cleanup descriptors` reuses the existing native fixture image. Four
rounds each fill the real table, verify file preservation and absent-file
creation refusal, reject eight pipes with zero free descriptors and eight
with one free descriptor, and require the pipe output array to remain unchanged.
After a second descriptor is released, a new pipe must transfer data and return
EOF after its writer closes. Capability capacity must match before/after
rejected pipes and after each complete round. Separate checks cover invalid
F_DUPFD minima, the highest valid minimum, descriptor flags, and exhaustion
with independent open descriptions rather than aliases. This measures the
caller's capability capacity, not global kernel memory reclamation.

`tools/dash_descriptor_cases.py` supplies 61 shared interactive checks. A
launcher reserves descriptors 11 through 31, leaving 10 for Dash's normal
CLOEXEC job-control terminal. The commands themselves occupy/release 3 through
9 using ordinary `exec` redirections. Silt reaches its actual 32-entry table;
Linux receives a private RLIMIT_NOFILE of 32. No fault injector or account-wide
limit is used for these descriptor cases.

At each of zero, one and two free slots, the suite rejects three- and four-stage
pipelines four times each. Two slots admit the first stage and then leave only
one free slot for the next pipe: failure therefore occurs after a partial job
exists. Each failure must return status 2, preserve the existing running job,
remove the partial job and restore the same interactive prompt. Failed open
and target-save redirections additionally exercise stdout preservation and
new-source cleanup. Permanent closes must release descriptors successfully.

After each scenario, descriptor occupancy must return to baseline and a fresh
pipeline must transfer its exact payload. The reference consumer is Linux
`cat`; the target uses the existing fixture's `check-cleanup copy` mode. This
avoids requiring Dash's internal high-numbered input-buffer pipe descriptors
while the launcher's high descriptors are intentionally reserved. An early
reference run using the `read` builtin encountered that separate input-buffer
limit; it is retained in `build/d4-descriptor-linux-v2.*` and `v3.*` and is not
counted as a pass. The consumer substitution does not change the tested shell
pipeline construction or the payload assertion.

Linux counts the shell's actual `/proc/PID/fd` entries. Silt's existing `fds`
fixture counts descriptors inherited across exec, so arbitrary CLOEXEC leaks
are outside that shell-level count. The native capability and pipe-refill
checks provide separate libc evidence. The unrelated background job must reap
with SIGTERM status and the final job table must be empty.

## Verification

All eight host gates pass. A focused SMP-8 UART run passes all 61 new
interactive descriptor checks. Six full fresh-media runs pass on the final
frozen artifacts:

| Run | Passed | Failed | Seconds |
|---|---:|---:|---:|
| rootfs-1-smp1 | 311 | 0 | 110.24 |
| rootfs-2-smp4 | 311 | 0 | 102.23 |
| rootfs-3-smp8 | 311 | 0 | 219.70 |
| rootfs-4-smp8 | 311 | 0 | 218.20 |
| rootfs-5-smp8 | 311 | 0 | 223.55 |
| rootfs-6-smp8 | 311 | 0 | 216.95 |

Each full run includes four terminal cycles, 160 repeated prompt interrupts
and four induced ttyd/sessiond crashes. The matrix includes 366 shared descriptor
checks and six native descriptor-fixture runs (384 zero/one-slot pipe refusals).
Sixteen Linux runs each pass the 61 descriptor, 54 job/trap, 60 process-resource
and 16 forced read-signal checks: 64 reference sessions, all cleaned up. Their
frozen tools and Linux binary match the final inputs; the later native fixture
and libc-message corrections do not change that reference binary or its tools.

`build/d4-descriptor-accepted/` retains artifacts, commands, return codes,
durations, complete UART/PTY logs, Linux cleanup records, exact sources and
hash manifests. All 31 frozen implementation/test inputs, current kernel/rootfs/Dash
artifacts and per-run kernel/ttyd symbols match. The QEMU runner uses a new
private copy of each writable medium for every boot. Earlier failed development
runs remain separate and are not included in this matrix.

| Artifact | SHA-256 |
|---|---|
| Neva kernel | `558041de8305ccb37d2890d845a4d2e529400458661430c39610ade319a6dd6e` |
| Silt rootfs | `97c07d0f68a7c3dfb393646e8ad90b6e2535fb7dc1ac2ae491256d519fc481f4` |
| Silt Dash | `f7cfddad0e2d6337ffed783cc55b35b429440a270a2629de489ca341150ec43c` |
| Linux Dash | `ab77c7a497e89c02c74503431eaed7d7b26afc621059064939d1a8d3e3415085` |

Run the build and eight host gates with `meson compile -C build` and
`meson test -C build --print-errorlogs`. Run the full target gate with:

```sh
python3 tools/test_rootfs.py --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson --rootfs build/silt-rootfs.img \
  --smp 8 --terminal-cycles 4 --prompt-interrupts 160 --restart-tests \
  --uart-log build/descriptor-run.uart.log --gdb-log build/descriptor-run.gdb.log
```

Development evidence includes the native old-libc failure and the old Linux
Dash rejection in `build/d4-descriptor-negative-v2.*`. The latter loses stdout
after the full-table borrowed-source redirection; it cannot produce the next
completion marker. `build/d4-descriptor-linux-before.*` separately retains the
older case set's failed permanent close. These are expected negative controls,
not passing acceptance runs.

The initial frozen Linux launch attempts had artifact-packaging errors: a
copy lacked executable permission, then its `dash-linux` basename prevented
nested `dash` lookup. The corrected reference lives at `linux-reference/dash`,
preserves executable mode and has the same recorded bytes. Those failed
launches remain in `linux-stress.log` and `linux-stress-v2.log`; only the corrected
`linux-stress-v3.log` is acceptance evidence. An initial native fixture link
also rejected use of unimplemented `unlink`; the fixture now uses unique
absent paths and private disposable media.

The first full target run stopped at native fixture setup (code 100), before
any descriptor assertion. The earlier Dash cases had populated PrivateTmp's
single file slot with `/tmp/dash.out`, so creating a differently named file
returned EEXIST (errno 17 in the diagnostic run). The fixture now reuses
`/tmp/dash.out`; provider limits are not changed. The first failed matrix and the diagnostic run are retained in
`build/d4-descriptor-final/` and `build/d4-descriptor-setup-diagnostic.*`.
The absent O_CREAT check asserts EMFILE and absence within this bounded
provider; it does not demonstrate a second PrivateTmp file could be created
with capacity available.

The next full run reached the zero-free-slot shell checks but rejected Silt's
"unknown error" diagnostic: its `strerror` lacked EMFILE. That run is retained
in `build/d4-descriptor-error-message/`. Adding the missing EMFILE/ENFILE messages
is part of the final libc change; its runtime artifacts require a fresh matrix.

Six separate Linux comparisons against patchset 10 retain identical status
and stdout for nested group/function redirections, closing-only exec lists,
`command exec`, mixed close/duplicate lists and assignment-prefixed exec.
`redirection-compatibility.json` retains the commands and both outputs.

## Remaining scope

D4 remains open. Address-space and capability exhaustion in other construction
phases, terminal-transfer and stage-resume refusal, signal-zero probing,
independently delivered signals while a foreground job runs, orphaned-group
EIO, and full controlling-terminal/PTY semantics require separate acceptance.
The high-descriptor input-buffer limit described above and signal interruption
of other descriptor-construction paths also remain open. These results do not
claim full upstream/POSIX conformance or physical ARM qualification.
