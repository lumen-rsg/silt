# D4: terminal handoff and stage-resume refusal

## Contract and implementation, 2026-09-13

Dash reports a failed terminal handoff or deferred child resume as a construction
error (interactive status 2), reclaims the partial job, and returns the terminal
to the shell. Existing jobs retain their process/group capabilities.

Previously, `silt_pipeline_end` and `silt_job_finish` returned void and silently
swallowed these failures. Resume cleanup selected only suspended children, so
an earlier resumed stage escaped that cleanup. Dash then waited for a job whose
libc child records had been partially or completely removed, losing its prompt.

`SiltChild.constructing` now tracks construction ownership separately from its
suspended state. The marker survives each successful resume and clears only
when every stage starts. Abort terminates all construction members before
waiting for any of them, retries interrupted waits, then releases the retained
process and group capabilities. Existing jobs have already cleared their
construction marker and are excluded. A handoff failure returns EIO; a resume
failure returns EAGAIN after cleanup.

The internal finish/end hooks now return an integer result. Maintained patch
`0007-report-job-admission-refusal.patch` keeps the pipeline exception guard
installed through `silt_pipeline_end` and raises on failure. Standalone
`forkshell` and external-command `vforkexec` paths also abort their Dash job
record and raise. The existing `abortjob` terminal restoration runs after child
reclamation. Native terminal/cleanup fixtures check the new return value.
Source preparation advances to patchset 12 using a fresh verified upstream
archive. The kernel and process/descriptor limits are unchanged.

Resume is sequential. Earlier stages may execute before a later stage is
refused; cleanup cannot undo their externally visible side effects. This is a
resource-ownership and prompt-recovery contract, not atomic pipeline execution.
It also does not qualify a second refusal during terminal restoration or an
independent failure of retained-capability termination/reaping.

## Targeted runtime gate

`tools/test_pipeline_admission.py` boots private disposable media, waits for the
service graph and recovery prompt, and launches interactive Dash. Each run
contains 26 selected refusals and 142 assertions:

- Foreground three-stage pipeline handoff refusal.
- First, middle and final stage-resume refusal for foreground and background
  three-stage pipelines.
- Foreground handoff refusal and foreground/background resume refusal for
  standalone subshells and external commands.
- Two rounds of every case, each requiring a diagnostic, status 2, preserved
  unrelated running job, restored inherited descriptor count and a successful
  foreground pipeline carrying an exact payload.
- Reaping the unrelated job with SIGTERM status, an empty job table, and
  filling/reaping all six session child slots after the refusals.

`tools/pipeline_fault.py` attaches GDB to the exact kernel used for the boot.
It filters requests by the interactive shell's PID and the requested boundary,
then modifies one request before normal dispatch:

- Handoff: increment the generation argument of session-control method 25.
  The transferred group is unchanged, so normal sessiond authority validation
  rejects the stale generation before changing the terminal foreground.
- Resume: replace the selected Process RPC method 5 with unsupported method
  65535 at the handler entry. The selected child must still be suspended.
  Earlier resume requests and their consumed-resume flags are recorded before
  a middle/final refusal. The normal handler returns NOT_SUPPORTED.

The injector writes only that request field: the saved syscall argument for
handoff or the dispatched message method for resume. It does not edit process
state, scheduler locks, capability tables, reply values or lifecycle counters.
It removes its breakpoint and detaches after the selected request. Missing
injection markers fail the gate; command and debugger waits are bounded. GDB
changes scheduling, so this gate demonstrates selected failure recovery rather
than the natural frequency of a race. Normal full-rootfs runs provide separate
runtime regression evidence.

The descriptor fixture counts descriptors inherited across exec; it does not
count arbitrary CLOEXEC capability leaks. The final process-quota refill checks
that partial construction members do not retain child admission charges. These
are bounded observations, not a global heap or physical-machine qualification.
The Linux reference uses successful no-op hooks and checks shell regressions;
it cannot reproduce Silt's deferred-resume protocol.

## Negative controls and development evidence

The frozen pre-fix image from Silt `1903c2e` loses the prompt after both a
handoff refusal and a second-stage resume refusal. The latter records the first
child's consumed resume before refusing the second. The UART and GDB records
are retained under `build/d4-admission-before/handoff-v4/` and
`build/d4-admission-before/resume/`, with their adjacent driver logs. They are
expected failures, excluded from acceptance.

Earlier driver attempts are also retained: the boot marker originally omitted
a space, prompt setup initially matched its own echo, and the descriptor
marker used the wrong name. A subsequent corrected-image run passed the
refusal cases but treated Dash's final Terminated notification as a live job;
the gate now consumes that notification and checks the next job listing.
`build/d4-admission-probe-v3/` records the 142-check SMP-8 development pass.

## Verification

All eight host gates pass, including the prepared Linux reference checks.
Six pairs of fresh-media QEMU runs pass on frozen artifacts: 1 and 4 CPUs,
followed by four consecutive 8-CPU pairs. Each row contains two independent
boots, one for the full rootfs suite and one for selected admission refusals.

| CPUs | Full rootfs | Seconds | Admission | Seconds |
|---|---:|---:|---:|---:|
| 1 | 311/311 | 111.94 | 142/142 | 19.39 |
| 4 | 311/311 | 98.44 | 142/142 | 14.89 |
| 8 | 311/311 | 213.87 | 142/142 | 35.32 |
| 8 | 311/311 | 201.59 | 142/142 | 36.20 |
| 8 | 311/311 | 223.83 | 142/142 | 33.97 |
| 8 | 311/311 | 218.77 | 142/142 | 33.97 |

Each full-rootfs run includes four terminal cycles, 160 prompt interrupts and
four induced ttyd/sessiond crashes. Across the six admission runs, all 156
selected refusals recover; the logs record 72 already-resumed prefix members
across 48 middle/final refusals. An independent UART-log check requires exactly
one running holder in each of the 156 post-refusal job listings, including
external-command cases, and an empty final job table in every run.

`build/d4-admission-accepted/` retains frozen artifacts, 74 source inputs,
commands, return codes, durations, host output, full UART/GDB logs and hash
manifests. `finalize_evidence.py` verifies every frozen/current input and
artifact, per-run kernel and ttyd symbols, all injection/prefix counts and the
complete job listings; `verified.json` records the result. The negative-image
hashes and expected failures are recorded separately in
`build/d4-admission-before/negative-controls.json`.

| Artifact | SHA-256 |
|---|---|
| Neva kernel | `558041de8305ccb37d2890d845a4d2e529400458661430c39610ade319a6dd6e` |
| Silt rootfs | `43cd6162cd7adbd5c9d800f560f9ad2b2e33405b9b72ec727a108cecbb598a60` |
| Silt Dash | `97fe31e93e62168b6241f5b820e7fa5fc5f6298d8234fa05dda5d045c2ae6b8b` |
| Linux Dash | `d77473ef6d6916141fadaccc4ca87922ae226880d68a53cde04acbe365e7e413` |

## Reproduction and remaining scope

```sh
meson compile -C build
meson test -C build --print-errorlogs
python3 tools/test_pipeline_admission.py --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson --rootfs build/silt-rootfs.img \
  --smp 8 --output build/admission-run
python3 tools/test_rootfs.py --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson --rootfs build/silt-rootfs.img \
  --smp 8 --terminal-cycles 4 --prompt-interrupts 160 --restart-tests \
  --uart-log build/admission-full.uart.log --gdb-log build/admission-full.gdb.log
```

D4 remains open. Address-space/capability exhaustion in other construction
phases, input-buffer descriptor pressure, signal arrival during construction,
independently delivered foreground-job signals, signal-zero probing, orphaned
group EIO and full controlling-terminal/PTY semantics require separate
acceptance. This does not claim full upstream/POSIX conformance.
