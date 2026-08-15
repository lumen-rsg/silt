# dash port

The Silt shell port tracks upstream dash 0.5.13.5 at commit
`037bbdfd330017c368caf6242f977974123239b5`. Exact source provenance is stored
in `upstream.lock`.

Upstream source is downloaded into the ignored `vendor/` directory and must
not be hand-edited. Silt-specific configuration and ordered patches live in
this directory so that every divergence remains reproducible and reviewable.

The initial build deliberately defines `SMALL=1` and `JOBS=0`. This is a
bring-up boundary, not the intended final feature set. It allows the parser,
expansion engine, built-ins, evaluator, and execution machinery to compile
before Silt advertises descriptor and terminal behavior it does not yet
provide.

See `docs/plans/dash-port.md` for the enablement and acceptance sequence.
