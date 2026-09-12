# dash port

The Silt shell port tracks upstream dash 0.5.13.5 at commit
`037bbdfd330017c368caf6242f977974123239b5`. Exact source provenance is stored
in `upstream.lock`.

Upstream source is downloaded into the ignored `vendor/` directory and must
not be hand-edited. Silt-specific configuration and ordered patches live in
this directory so that every divergence remains reproducible and reviewable.

The build defines `SMALL=1` and `JOBS=1`. Job creation is bracketed by a small
Silt hook: forked children remain suspended until sessiond has attached the
whole pipeline and the foreground capability has been transferred to ttyd.
No line-editing library is linked.

Patch 0003 lets an input read leave dash's interrupt-deferred section when a
SIGINT is pending. The pinned source otherwise retries EINTR indefinitely in
that section. This was reproduced with its Linux no-libedit build as well as
Silt; `tools/test_dash_interrupt.py` checks three successive Linux PTY interrupts.
The source preparation script applies all three patches to the checked archive.

See `docs/plans/dash-port.md` for the enablement and acceptance sequence.
