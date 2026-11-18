@ Luma's own system calls the app uses (Luma3DS k11_extension; libctru doesn't have them).

.arm
.align 2

@ void svcInvalidateEntireInstructionCache(void): after code was written to memory (theme add-ons,
@ theme_loader.c). Not svc 0x93 (a range): the kernel does that on every core, and on another core
@ this process's addresses aren't mapped, so it faults (it crashed the console, 2026-11-17).
.section .text.svcInvalidateEntireInstructionCache, "ax", %progbits
.global svcInvalidateEntireInstructionCache
.type svcInvalidateEntireInstructionCache, %function
svcInvalidateEntireInstructionCache:
    svc 0x94
    bx  lr

@ Result svcControlService(u32 op, Handle *out, const char *name): op 0 = a session to a service
@ whose sessions are all taken, shared with the process holding one (autostart.c: Luma's loader).
@ The same as the tracker's (csvc.s), which shares mcu::HWC's.
.section .text.svcControlService, "ax", %progbits
.global svcControlService
.type svcControlService, %function
svcControlService:
    svc 0xB0
    bx  lr
