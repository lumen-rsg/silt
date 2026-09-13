# GNU coreutils C0 acceptance — 2026-09-13

## Candidate and scope

This starts the GNU coreutils port after Silt Dash D5 commit
`ce8bcd165ff71f66b7b5d88b014dc26a0471629f`. Neva remains at
`28831dc7c1265baf0a16099a6c48e02c475f981b` with no source changes in this slice.
The candidate is the Silt working-tree implementation described by
[the port README](../../ports/coreutils/README.md) and
[the milestone plan](../plans/coreutils-port.md).

Installed GNU commands: `/bin/echo`, `/bin/basename`, `/bin/dirname`. They share
one executable inode with `/bin/coreutils`; `/bin/sh` remains Dash and `/bin/true`
retains its dedicated pager/FP fixture. C0 is a bounded three-command port with
an output-only stdio baseline, not the complete GNU suite or full POSIX libc.
No physical hardware qualification was performed.

The C0 artifacts and host log were preserved in `build/coreutils-c0-frozen/`
before C1 replaced the normal `build/` outputs. Hashes below are historical C0
evidence; current sources and the default image now contain C1.

## Exact artifacts

| Artifact | SHA-256 |
|---|---|
| GNU 9.11 source archive | `394024eda0a5955217ceda9cd1201e65dc8fa3aa29c2951135a49521d57c3cc3` |
| Neva `build-meson/neva.elf` | `7043aa8b67f653110e6278da2f60b18ff6bfbdc95a3380148f465758585cf78b` |
| Silt `build/silt-rootfs.img` | `7135b4f8b7ca11dbb04b6d05d9423dff5b8c677559cccc9c12de4b0db1ae5e88` |
| Silt `build/coreutils.elf` | `13bca5a436ded661e892c85778c4c6849f24f1cb1f6d5069297076ea0aefe954` |
| Silt `build/dash.elf` | `a7b43046a6b2780d5ba2a13c1e15d56e0aa0fba7945a04a85c6109d371eab04c` |
| Silt `build/true.elf` | `0573bbcf02347c28a2d25eeb1f81e49c35ef851b8bc30969b4de6f6e25aab31c` |

The image is 8 MiB; `coreutils.elf` is 296,808 bytes including ELF/debug data.
Each differential run's `result.json` records kernel, service ELF, initial disk,
rootfs and Linux reference executable hashes. Fresh private disk copies are
created by the existing Neva QEMU harness. Runtime-created `/tmp/cu` belongs to
that run's private namespace.

## Checks

- Clean freestanding ARM64 build: all selected command and gnulib sources
  compile with warnings as errors. The six selected gnulib implementation files
  match the unpatched release byte-for-byte. Command patches only change includes.
- Silt host/reference/image suite: **15/15 passed** in
  `build/coreutils-host.log`. Includes existing Dash reference gates, ELF symbol
  closure, exact image reconstruction, static ARM64/W^X/entry checks, installed
  bytes, hardlink count/modes and installed GPL text.
- New native stdio tests inject partial writes, EINTR, no progress, terminal
  errors, multiplication overflow, binary NUL, empty output, allocation refusal,
  failed close and sticky error state. Exit tests exercise all 32 registrations,
  overflow refusal, reverse drain order and callback registration during exit.
  These transport injections are host tests; target closed-descriptor failures
  are exercised separately in the QEMU cases.
- The unpatched Linux reference is built from the checked release with
  `tools/prepare_coreutils_reference.py`; its clean run is recorded in
  `build/coreutils-reference-prepare.log`. These are original differential tests,
  not a claim to have run the release's full upstream test suite.
- Each focused target run checks **77 cases**: 72 GNU/Linux comparisons and
  five shell integration cases. Data output and exit status are compared;
  help/version checks use identifying prefixes, and adapted diagnostic prose
  is excluded from byte equivalence. Cases cover GNU short/long options,
  abbreviations, operand errors, POSIXLY_CORRECT, suffix/root/slash/empty paths,
  control/NUL bytes, closed stdout, text/binary pipes, redirection, substitution
  and explicit multicall dispatch.
- Final image Dash release gate: **34/34 passed**, recorded in
  `build/coreutils-d5-final-smp4.log` and
  `build/coreutils-d5-final-smp4/uart.log`.

The broader retained rootfs regression passed **315/315** on eight CPUs with
four terminal cycles, 160 prompt interrupts and four service crashes. Logs:
`build/coreutils-rootfs-smp8.log` and `build/coreutils-rootfs-smp8.uart.log`.
It includes native signal/session/PTY/pipe checks, job/wait/trap behavior,
process quota and descriptor refusal/recovery. This is a regression pass at
the stated settings, not a rerun of D5's entire 35-boot release matrix.

Final focused matrix, on the exact same kernel, service, reference and disk
artifacts throughout:

| Run directory under `build/` | CPUs | Result |
|---|---:|---|
| `coreutils-c0-final-1-smp1` | 1 | 77/77 |
| `coreutils-c0-final-2-smp4` | 4 | 77/77 |
| `coreutils-c0-final-3-smp8` | 8 | Boot failure; 0 command cases executed |
| `coreutils-c0-final-4-smp8` | 8 | 77/77 |
| `coreutils-c0-final-5-smp8` | 8 | 77/77 |

The last two runs were consecutive and serialized after the broader regression
finished. They do not erase the preceding boot failure or constitute an entirely
clean first-attempt matrix. The final harness also rejects explicit failed boot
self-test lines; retained UARTs from the first two successful runs were checked
against that rule. All per-run artifact hashes were compared with each other and
with the files left in the build directories after testing.

[The machine-readable evidence](coreutils-c0-evidence.json) preserves baseline
commits, source/configuration hashes, artifact hashes and per-run log hashes.
The raw UARTs and logs remain at the recorded workspace paths. This closes the
C0 implementation and bounded command-validation slice; the pre-session SMP-8
boot failure is an open, unattributed observation. C1 and the wider filesystem,
stdio, locale and utility families remain open.

## Retained unsuccessful attempts

1. Initial source integration found missing `getenv`, gnulib `_GL_UNUSED`, and
   newlib FILE/header macro dependencies. These were fixed in Silt, followed by
   a clean rebuild; no newlib stream runtime was linked.
2. The first Linux build attempted direct command targets before generating
   all gnulib headers and produced stale/incomplete objects. The reference
   helper now generates `BUILT_SOURCES` first. A fresh reference preparation
   succeeds without modifying upstream source. Earlier logs remain in
   `build/coreutils-linux/`; accepted final runs use `build/coreutils-reference/`.
3. `build/coreutils-c0-smp4` failed the NUL comparison because the console
   filters NUL output. `build/coreutils-c0-v2-smp4` then refused creation of
   `cu.bin` in the initial session cwd. The harness now writes to the supported
   private `/tmp/cu` and reads binary data through `check-pipes hex`; a separate
   binary pipeline case also checks exact bytes. These attempts do not count
   as acceptance. The corrected earlier image passed 75 cases in
   `build/coreutils-c0-v3-smp4`; final evidence uses the 77-case image above.
4. `build/coreutils-c0-final-3-smp8` failed **before any command ran**:
   `B4_RESET_RESTART: FAIL stage=1`, followed by initd death and sealed recovery.
   The exact image and UART are retained. Another SMP-8 QEMU regression was
   running concurrently. That timing context is not proof of causation; the
   boot failure remains unattributed. Subsequent SMP-8 attempts are serialized.

## Reproduction

Run the build, Linux reference and focused gate commands from the port README.
The additional regression commands used here, from the Silt checkout, are:

```sh
meson test -C build --print-errorlogs
python3 tools/test_d5_release.py \
  --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson \
  --rootfs build/silt-rootfs.img --smp 4 \
  --output build/coreutils-d5-final-smp4
python3 tools/test_rootfs.py \
  --neva-source ../neva-microkernel \
  --neva-build ../neva-microkernel/build-meson \
  --rootfs build/silt-rootfs.img --smp 8 --restart-tests \
  --terminal-cycles 4 --prompt-interrupts 160 \
  --uart-log build/coreutils-rootfs-smp8.uart.log
```
