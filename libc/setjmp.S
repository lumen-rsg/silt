    .text
    .globl setjmp
    .type setjmp, @function
setjmp:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    str x29, [x0, #80]
    str x30, [x0, #88]
    mov x2, sp
    str x2, [x0, #96]
    stp d8, d9, [x0, #104]
    stp d10, d11, [x0, #120]
    stp d12, d13, [x0, #136]
    stp d14, d15, [x0, #152]
    adrp x2, g_silt_cleanup_head
    ldr x2, [x2, :lo12:g_silt_cleanup_head]
    str x2, [x0, #168]
    mov x0, #0
    ret

    .globl longjmp
    .type longjmp, @function
longjmp:
    // Unwind ownership before discarding stack frames or the signal mask.
    stp x0, x1, [sp, #-16]!
    ldr x0, [x0, #168]
    bl silt_cleanup_unwind
    ldp x0, x1, [sp], #16
    // Abandon a signal frame only if the target stack lies outside it.
    mov x9, x0
    ldr x0, [x9, #96]
    mov x8, #71
    svc #0
    mov x0, x9
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldr x29, [x0, #80]
    ldr x30, [x0, #88]
    ldr x2, [x0, #96]
    ldp d8, d9, [x0, #104]
    ldp d10, d11, [x0, #120]
    ldp d12, d13, [x0, #136]
    ldp d14, d15, [x0, #152]
    mov sp, x2
    cmp x1, #0
    cinc x0, x1, eq
    ret
