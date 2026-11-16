#ifndef HEAP_H
#define HEAP_H

// The tracker's heap (malloc). Empty at start: it grows a page at a time while a game
// with achievements runs (its achievements and conditions, the copies of its memory,
// the menu's icons) and shrinks back when that's freed and Heap_Trim() is called.
//
// This module shares the Base region with every system module, and the system
// crashes when that runs out: a kernel panic in fs on 2026-11-15, when a .3dsx was
// opened (Luma starts nearly every system module for one) and the tracker's old fixed
// 256 KB heap left 64 KB. So the heap never grows past what leaves HEAP_RESERVE to the
// system; a malloc that would returns NULL instead.

#include <3ds/types.h>

// The heap area: the main thread's stack first (libctru's initSystem moves the stack to
// the start of the heap area: __system_allocateHeaps maps it), then the heap.
#define HEAP_BASE       0x08000000u
#define HEAP_LIMIT      0x100000u       // the most the heap grows to (Ocarina of Time needs ~170 KB)
#ifndef HEAP_RESERVE                    // (a test build can make it a variable)
#define HEAP_RESERVE    (192 * 1024)    // memory it always leaves to the system
#endif

// Call first in main(): frees only shrink the heap through Heap_Trim().
void    Heap_Init(void);

// Memory the system modules can still get: the least of what their shared limit
// allows and what's free in the Base region. Both separately, for the log.
u32     Heap_SystemFree(void);
u32     Heap_LimitFree(void);
u32     Heap_RegionFree(void);

// Bytes the heap holds now.
u32     Heap_Size(void);

// Growths refused (too little memory left) since the last call.
u32     Heap_TakeRefused(void);

// Gives back to the system what malloc isn't using.
void    Heap_Trim(void);

#endif
