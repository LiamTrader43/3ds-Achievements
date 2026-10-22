@ Luma3DS kernel-extension calls (copy of tracker/source/csvc.s), used to read the tracker's status.
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
