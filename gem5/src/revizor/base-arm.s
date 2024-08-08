// entry point for GNU ld
.global _start
// address where revizor will insert its code
.global code
// base address of memory sandbox for code to play with
.global sandbox

.data
.balign 4096
sandbox:
    .fill 8192
start_sp:
    .quad 0
registers:
    .fill 116

.text
_start:
    // store stack pointer
    ldr x0, =start_sp
    mov x1, sp
    str x1, [x0]
    dsb sy
    isb
    b code
.balign 4096
code:
    .fill 1024, 4, 0xD503201F // noops
    dsb sy
    isb
    // call m5_exit(0)
    mov x0, #0
    bl m5_exit
    // restore stack pointer
    ldr x0, =start_sp
    ldr x0, [x0]
    mov sp, x0
    // load input register values
    ldr x30, =registers
    ldp x0, x1, [x30], #16
    ldp x2, x3, [x30], #16
    ldp x4, x5, [x30], #16
    ldp x6, x7, [x30], #16
    ldp x8, x9, [x30], #16
    ldp x10, x11, [x30], #16
    ldp x12, x13, [x30], #16
    ldp x14, x15, [x30], #16
    ldp x16, x17, [x30], #16
    ldp x18, x19, [x30], #16
    ldp x20, x21, [x30], #16
    ldp x22, x23, [x30], #16
    ldp x24, x25, [x30], #16
    ldp x26, x27, [x30], #16
    ldp x28, x29, [x30], #16
    // set x30 to sandbox base address
    ldr x30, =sandbox
    b code
    
