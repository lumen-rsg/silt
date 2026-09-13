# GNU coreutils for Silt

C2a builds **GNU coreutils 9.11** `echo`, `basename`, `dirname`, `cat`, `head`,
`tail`, `wc`, `pwd`, `printenv`, and `yes` as a static ARM64 `/bin/coreutils` image. Each command name is a hard link to that inode.
Explicit `/bin/echo` selects GNU echo; Dash's `echo` builtin retains its own
semantics. `/bin/coreutils dirname a/b` also dispatches a selected command.

[upstream.lock](upstream.lock) records the release commit, gnulib revision,
archive URL, published SHA-256 and license. The digest was checked against the
[GNU release announcement](https://lists.gnu.org/archive/html/coreutils-announce/2026-04/msg00000.html).
`tools/prepare_coreutils.py` verifies the archive on every invocation and applies
the ordered patches with zero fuzz. Prepared sources under `vendor/` are ignored;
change the checked-in patch/configuration, never a prepared source tree. A stale
or incomplete prepared tree is refused rather than overwritten.

The ordered patches replace broad platform includes in the selected commands,
exclude tail's follow/monitoring implementation and wc's multibyte/file-list
paths, and give those two commands profile-specific help. The finite tail
file/pipe algorithms and wc's single-byte counting algorithms are retained.
Cat, head, pwd, printenv and yes change only their includes. Yes uses the
upstream ordinary-write path with Linux splice disabled. All eighteen selected gnulib source
files compile unchanged, including checked count conversion and safe/full I/O.
Per-command `main` and `usage` names are renamed for the Silt dispatcher.

| Command | Supported profile | Deliberate boundary |
|---|---|---|
| `cat` | File/stdin/multiple operands; numbering, squeezing and byte display options | Ordinary descriptor copy; no Linux splice/copy acceleration |
| `head` | Lines/bytes, signed counts, GNU suffixes, headers, NUL delimiters | Subject to Silt memory and descriptor quotas |
| `tail` | Finite lines/bytes on files and pipes, +NUM, headers, NUL delimiters | Follow, retry, PID monitoring and sleep options fail with status 1 |
| `wc` | Lines/words/bytes/characters/display width, operands and totals | Single-byte C locale; -m counts bytes; --files0-from fails with status 1 |
| `pwd` | Physical/logical paths, PWD validation, last-option precedence | Existing namespace, bounded paths; no filesystem symlinks or retained cwd under rename |
| `printenv` | All/selected variables, duplicate entries, empty values, NUL output | Existing environment/exec budgets; output errors return status 2 |
| `yes` | Repeated default or joined operands, GNU option parsing, SIGPIPE/error exit | Ordinary descriptor writes; existing 127-byte total argv budget |

GNU wc's C-locale NBSP separator is retained; POSIXLY_CORRECT disables it.
Other locale settings do not enable multibyte processing. Diagnostic prose and
help/version scaffolding are adapted, rather than a byte-equivalence claim.

The C1 adapter adds allocation/alignment, integer formatting for counters and
EINTR-safe reads. Advisory access hints, binary-mode selection and purging
unbuffered standard streams require no transport action. Unsupported copy
acceleration returns ENOSYS, letting GNU cat use its normal copy loop.
Silt libc now reports pipe descriptors as FIFO and preserves the full 64-bit
volume/object identity pair in `st_dev`/`st_ino`. Distinct providers can reuse an
object ID, and private temporary IDs exceed 32 bits.
Integer parsing has signed-limit and unsigned-overflow checks; Dash uses libc's
stpcpy and its required-symbol contract records that dependency.

`config.h` is a manually audited configuration for these translation units.
It is **not** an Autoconf result for all of coreutils or gnulib. `support.c`
provides C-locale diagnostics, allocation failure, version/help scaffolding,
and output-close checking. Diagnostic quoting and help/version boilerplate
are Silt adaptations; they are not byte-identical to GNU/Linux. Option parsing,
pathname operations, data output and exit statuses are differential-tested.

The GNU GPL is installed at `/usr/share/licenses/coreutils/COPYING`. Command
sources retain their GPL-3.0-or-later notices; imported gnulib files retain their
individual GPL/LGPL notices in the verified source archive.

C2a adds unchanged gnulib long-option parsing, exit status policy, xgetcwd and
root identity helpers. GNU pwd can use its chdir/stat fallback when dirfd is
unavailable; Silt's directory streams retain a capability rather than a POSIX
fd. The fallback remains compiled, but target acceptance exercises the normal
getcwd path. It does not qualify recovery after directory rename/deletion.

The libc fixes reject undersized allocating getcwd requests before allocation,
remove the extra slash left by parent-component normalization, and distinguish
the virtual root from synthetic character-device metadata. GNU printenv's
status-2 failure policy now reaches the output-close handler. Existing commands
retain the default status-1 policy.

## Build and test

From the Silt checkout with the sibling Neva SDK already built:

```sh
python3 tools/setup_meson.py build
meson compile -C build
meson test -C build --print-errorlogs
python3 tools/prepare_coreutils_reference.py --output build/coreutils-reference
meson compile -C build coreutils-qemu-test
meson compile -C build coreutils-streams-qemu-test
meson compile -C build coreutils-inspection-qemu-test
```

The Linux reference is built from an **unpatched** extraction of the same
archive. The preparation helper first generates all `BUILT_SOURCES`, then
builds only the ten command targets. Configure and the gnulib library still
require the normal Linux native build dependencies. Reference preparation is
explicit and is not part of the freestanding target setup.

The QEMU gate requires the reference directory and records UART output,
per-case results and artifact hashes. It checks actual installed hardlinks,
starts at the normal Dash prompt after initd readiness, and boots private
copies of all writable disks. `--smp` and `--output` support independent runs:

```sh
python3 tools/test_coreutils.py \
  --reference build/coreutils-reference/build/src \
  --smp 8 --output build/coreutils-c0-smp8
```

These are original differential cases, not the complete upstream test suite.
Binary output is checked through `/tmp/cu` or a pipe using `check-pipes hex`;
raw console output is unsuitable because the console filters NUL bytes.
C1 compares byte length plus FNV-1a of command output using `check-pipes digest`,
alongside the producer and consumer statuses. Fixtures span every byte value,
multiple buffers, long lines, unterminated lines and empty input. Run C1 with:

```sh
python3 tools/test_coreutils_streams.py --smp 8 --output build/coreutils-c1-smp8
```

The commands remain subject to Silt's bounded filesystem and process profile;
full upstream coreutils, complete stdio and physical hardware acceptance remain
outside this milestone.

See [the port plan](../../docs/plans/coreutils-port.md) and
[the C0 evidence](../../docs/architecture/coreutils-c0-acceptance.md) and
[the C1 evidence](../../docs/architecture/coreutils-c1-acceptance.md) and
[the C2a evidence](../../docs/architecture/coreutils-c2a-acceptance.md).
