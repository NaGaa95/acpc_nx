.section .text.nx_guide_capture,"ax",%progbits
.align 2

.global nx_capture_guide_presenter
.type nx_capture_guide_presenter,%function
nx_capture_guide_presenter:
    sub sp, sp, #96
    sub sp, sp, #160
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    str x18, [sp, #144]
    str x30, [sp, #152]
    bl nx_capture_guide_presenter_c
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldr x18, [sp, #144]
    ldr x30, [sp, #152]
    add sp, sp, #160
    adrp x16, g_guide_capture_resume
    add x16, x16, #:lo12:g_guide_capture_resume
    ldr x16, [x16]
    br x16
.size nx_capture_guide_presenter, .-nx_capture_guide_presenter
