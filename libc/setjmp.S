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
    mov x0, #0
    ret

    .globl longjmp
    .type longjmp, @function
longjmp:
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldr x29, [x0, #80]
    ldr x30, [x0, #88]
    ldr x2, [x0, #96]
    mov sp, x2
    cmp x1, #0
    cinc x0, x1, eq
    ret
