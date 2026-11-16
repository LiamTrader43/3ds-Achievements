#include "heap.h"

#include <3ds.h>
#include <errno.h>
#include <malloc.h>
#include <reent.h>

extern char *fake_heap_start;

static u32 s_base;                      // where the heap starts (0 until the first call)
static u32 s_end;                       // newlib's break
static u32 s_mapped;                    // the end of the pages taken
static u32 s_refused;                   // growths refused: too little left

// The heap starts after the main thread's stack: libctru's initSystem puts the stack at
// the start of the heap area (fake_heap_start, which __system_allocateHeaps sets) and
// moves fake_heap_start past it.
static void Start(void)
{
    if (s_base == 0)
        s_base = s_end = s_mapped = ((u32)fake_heap_start + 0xFFF) & ~0xFFFu;
}

u32 Heap_LimitFree(void)
{
    Handle              limit;
    ResourceLimitType   type = RESLIMIT_COMMIT;
    s64                 max = 0, used = 0;

    if (R_FAILED(svcGetResourceLimit(&limit, CUR_PROCESS_HANDLE)))
        return 0;
    svcGetResourceLimitLimitValues(&max, limit, &type, 1);
    svcGetResourceLimitCurrentValues(&used, limit, &type, 1);
    svcCloseHandle(limit);
    return max > used ? (u32)(max - used) : 0;
}

u32 Heap_RegionFree(void)
{
    return osGetMemRegionFree(MEMREGION_BASE);
}

u32 Heap_SystemFree(void)
{
    u32 limit = Heap_LimitFree(), region = Heap_RegionFree();
    return limit < region ? limit : region;
}

// newlib's malloc asks for more (or less) heap through this.
void *_sbrk_r(struct _reent *r, ptrdiff_t increment)
{
    Start();
    u32 old = s_end, end = old + (u32)increment, out;

    if ((increment > 0 && (end < old || end > s_base + HEAP_LIMIT)) || (increment < 0 && (end > old || end < s_base)))
    {
        r->_errno = ENOMEM;
        return (void *)-1;
    }

    u32 mapped = (end + 0xFFF) & ~0xFFFu;
    if (mapped > s_mapped)
    {
        u32 size = mapped - s_mapped;
        if (Heap_SystemFree() < size + HEAP_RESERVE
            || R_FAILED(svcControlMemory(&out, s_mapped, 0, size, MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE)))
        {
            s_refused++;
            r->_errno = ENOMEM;
            return (void *)-1;
        }
        s_mapped = mapped;
    }
    else if (mapped < s_mapped && R_SUCCEEDED(svcControlMemory(&out, mapped, 0, s_mapped - mapped, MEMOP_FREE, 0)))
        s_mapped = mapped;
    s_end = end;
    return (void *)old;
}

void Heap_Init(void)
{
    Start();
    // achievements.c counts on room it has just freed staying there (ParseRoom), so a
    // free never gives memory back by itself: only Heap_Trim does.
    mallopt(M_TRIM_THRESHOLD, HEAP_LIMIT + 1);
}

u32 Heap_Size(void)
{
    return s_mapped - s_base;
}

u32 Heap_TakeRefused(void)
{
    u32 refused = s_refused;
    s_refused = 0;
    return refused;
}

void Heap_Trim(void)
{
    malloc_trim(0);
}
