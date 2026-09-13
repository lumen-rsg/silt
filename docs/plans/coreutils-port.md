# GNU coreutils port

## Objective and ownership

Grow Silt's usable command-line userland after the committed Dash D5 release.
Silt owns third-party sources, libc/POSIX adaptation, commands, tests and image
assembly. Neva owns capability mechanisms and the ABI. Add a kernel primitive
only when an audited libc/service requirement cannot be expressed through the
existing capability interfaces; executable names must not gain ambient authority.

Target GNU coreutils 9.11, pinned by archive digest and upstream revision in
`ports/coreutils/upstream.lock`. Port real upstream command sources with narrow
reviewable adapters. A small initial command set does not establish full GNU
coreutils support or general OS POSIX conformance.

## C0: source/build/runtime foundation

Implemented: GNU `echo`, `basename`, and `dirname`; upstream pathname helpers
and GNU getopt; one executable with command hardlinks; verified source
preparation; unpatched pinned Linux reference; image/ELF checks and original
QEMU differential cases. See [C0 acceptance](../architecture/coreutils-c0-acceptance.md).

The slice adds descriptor-backed unbuffered standard output/error streams,
short-write/EINTR handling, sticky stream errors, `getenv`, and 32 `atexit`
registrations. Returning from `main` calls `exit`; `_exit` continues to bypass
callbacks. Silt retains the toolchain's opaque FILE declaration for wchar header
compatibility, while owning stream state separately. No newlib implementation
or reentrancy globals are linked. This is an output-only standard-stream
baseline, not a complete stdio implementation. Existing `vsnprintf` supports
only the documented integer/string subset needed here; C0 does not claim full
printf format support, stream buffering, fopen/fread, or async-signal safety.

`/bin/true` remains the existing dedicated two-page pager/FP-state fixture.
`/bin/false`, `id`, `uname`, and other Silt diagnostics retain their existing
implementations. They are not represented as GNU ports. A future GNU `true`
replacement requires moving and requalifying Neva's current executable fixture.

## C1: byte streams and basic file inspection

Implemented GNU `cat`, `head`, finite `tail` and single-byte C-locale `wc`.
The port retains file/pipe algorithms, GNU count suffixes and transformations;
follow/monitoring and filename-list modes fail explicitly. `wc -m` counts
single-byte C-locale characters, not multibyte Unicode characters. See the
[profile and adapter details](../../ports/coreutils/README.md) and
[C1 acceptance](../architecture/coreutils-c1-acceptance.md).

The source audit exposed two libc prerequisites: pipe fstat must report FIFO
without sending file RPCs to a ByteStream, and inode/device types must preserve
the full 64-bit volume/object identity pair. Both are implemented in Silt. Conversion
boundary fixes and EINTR-safe reads are tested separately. No Neva primitive,
quota increase or ambient authority was added.

Acceptance covers regular files/stdin/multiple operands, all byte values,
lines larger than the buffers, partial pipe transfers, count limits, GNU word
classification, closed-output and directory errors, same-file refusal, early
consumer exit and descriptor recovery. The pinned Linux release remains the
oracle; the target runs boot fresh media through the normal Dash session.

## C2a: directory/environment inspection and pipeline producer

Implemented GNU `pwd`, `printenv` and `yes`, retaining all three upstream
command algorithms. This combines the first directory-inspection utility with
small environment and stream commands usable through the existing interfaces.
See [C2a acceptance](../architecture/coreutils-c2a-acceptance.md).

PWD validation exposed a synthetic-root identity collision with character
devices. Physical cwd checks exposed an extra separator after `..` normalization;
allocating getcwd had an undersized-buffer leak. These are fixed in libc and
covered by native and target tests. Printenv preserves status 2 on output
failure. Yes exercises actual SIGPIPE and repeated early-consumer cleanup.
No service or kernel mutation interface, authority or quota was enlarged.

## C2b: directory listing and mutation commands

Port `ls`, `mkdir`, `rmdir`, `rm`, `cp`, `mv`, `ln`, `touch`, and metadata
commands in separate bounded slices. Resolve required provider contracts before
advertising options. D5's current filesystem profile explicitly lacks general
shared file offsets across fork, retained cwd under rename, filesystem symlinks
and broad metadata mutation. Private `/tmp` holds one bounded file with fixed
0600 policy; the mutable NevFS overlay is also bounded. Directory stat modes are
synthetic 0555; libc link counts currently report 1.

Acceptance must cover identity/ownership, attenuation, permission and quota
refusal, cross-provider behavior, partial-operation cleanup and persistence as
appropriate. Review the service/kernel contracts when a command exposes a real
gap; do not enlarge limits solely to make a test fit.

## C3: text, numeric, process and environment families

Grow stdio, locale/multibyte handling, conversions, environment mutation, clocks
and process-facing adapters as required by `sort`, `uniq`, `cut`, `tr`, `printf`,
`seq`, `env`, `sleep`, `date`, and remaining families. C-locale limitations and
unsupported flags must remain explicit. Full GNU printf, locale collation and
Unicode behavior are not provided by C0's formatting helper.

## C4: release profile

Publish the command/option support matrix, source/patch provenance and installed
licenses. Run attributable upstream tests where their dependencies are available,
plus shared Linux/Silt cases, focused failure injection, shell integration and
resource recovery. Freeze the exact image before release matrices. Retain
separate evidence for host builds, QEMU and physical hardware; neither a static
link nor QEMU establishes physical qualification.

The existing eight-process session quota, 32 descriptors and 16 FSD executable
bindings remain relevant. Hardlinked command dispatch conserves executable
bindings without changing those kernel/service contracts. General pager binding
reclamation, complete stdio and full upstream utility coverage remain open.
