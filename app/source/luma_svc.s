@ Luma's own system calls the app uses (Luma3DS k11_extension; libctru doesn't have them).

.arm
.align 2

@ void svcInvalidateInstructionCacheRange(void *addr, u32 len): after code was written to memory
@ (theme add-ons, theme_loader.c).
.section .text.svcInvalidateInstructionCacheRange, "ax", %progbits
.global svcInvalidateInstructionCacheRange
.type svcInvalidateInstructionCacheRange, %function
svcInvalidateInstructionCacheRange:
    svc 0x93
    bx  lr
