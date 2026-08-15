// Silt process entry. Neva provides argc, argv, and an optional opaque exec
// handoff address in x0-x2. The C entry performs personality reconstruction.

.global _start
.extern silt_start

.text
_start:
    bl      silt_start
1:
    wfe
    b       1b
