@ Luma3DS kernel-extension calls used by the tracker (not in libctru).
@ Same calling convention as Luma's own sysmodules/rosalina/source/csvc.s.

    .arm
    .align 2

@ Result svcMapProcessMemoryEx(Handle dstProcess, u32 dstAddress, Handle srcProcess,
@                              u32 srcAddress, u32 size, MapExFlags flags)
    .section .text.svcMapProcessMemoryEx, "ax", %progbits
    .global svcMapProcessMemoryEx
    .type svcMapProcessMemoryEx, %function
svcMapProcessMemoryEx:
    push    {r4, r5, r6}
    ldr     r4, [sp, #12]
    ldr     r5, [sp, #16]
    mov     r6, r0              @ dst handle moves to r6 to make room for the magic value
    mov     r0, #0xFFFFFFF2     @ selects the 6-argument form
    svc     0xA0
    pop     {r4, r5, r6}
    bx      lr

@ Result svcUnmapProcessMemoryEx(Handle process, u32 address, u32 size)
    .section .text.svcUnmapProcessMemoryEx, "ax", %progbits
    .global svcUnmapProcessMemoryEx
    .type svcUnmapProcessMemoryEx, %function
svcUnmapProcessMemoryEx:
    svc     0xA1
    bx      lr

@ Result svcCustomBackdoor(void *func, ...): runs func(args...) in kernel mode.
    .section .text.svcCustomBackdoor, "ax", %progbits
    .global svcCustomBackdoor
    .type svcCustomBackdoor, %function
svcCustomBackdoor:
    svc     0x80
    bx      lr

@ Result svcControlProcess(Handle process, ProcessOp op, u32 varg2, u32 varg3)
    .section .text.svcControlProcess, "ax", %progbits
    .global svcControlProcess
    .type svcControlProcess, %function
svcControlProcess:
    svc     0xB3
    bx      lr

@ void svcFlushDataCacheRange(void *address, u32 length): clean + invalidate,
@ also for kernel addresses (used on framebuffers written in kernel mode).
    .section .text.svcFlushDataCacheRange, "ax", %progbits
    .global svcFlushDataCacheRange
    .type svcFlushDataCacheRange, %function
svcFlushDataCacheRange:
    svc     0x91
    bx      lr
