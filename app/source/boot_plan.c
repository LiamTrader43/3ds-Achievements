#include "boot_plan.h"

#include <string.h>

#define SYSMODULE_HIGH  0x00040130u     // a system module's title ID, high half

bool Boot_IsFirmModule(uint64_t id)
{
    switch (id & ~BOOT_N3DS_MASK)
    {
        case 0x0004013000001002ULL:     // sm
        case 0x0004013000001102ULL:     // fs
        case 0x0004013000001202ULL:     // pm
        case 0x0004013000001302ULL:     // loader
        case 0x0004013000001402ULL:     // pxi
            return true;
        default:
            return false;
    }
}

int Boot_ListDeps(const uint64_t raw[BOOT_MAX], bool n3ds, uint64_t out[BOOT_MAX])
{
    int count = 0;

    for (int i = 0; i < BOOT_MAX && raw[i] != 0; i++)
        if (n3ds || (raw[i] & BOOT_N3DS_MASK) == 0)
            out[count++] = raw[i] & ~BOOT_N3DS_MASK;
    return count;
}

// Adds the ids not listed yet to the end of the list, like pm's listMergeUniqueDependencies.
// False if the list would go past its limit (pm panics).
static bool Merge(uint64_t list[BOOT_MAX], int *listed, const uint64_t *ids, int count)
{
    for (int i = 0; i < count; i++)
    {
        int j = 0;
        while (j < *listed && list[j] != ids[i])
            j++;
        if (j < *listed)
            continue;
        if (*listed >= BOOT_MAX)
            return false;
        list[(*listed)++] = ids[i];
    }
    return true;
}

static const BootModule *Find(const Boot *boot, uint64_t id)
{
    for (int i = 0; i < boot->moduleCount; i++)
        if (boot->modules[i].id == id)
            return &boot->modules[i];
    return NULL;
}

void Boot_Read(Boot *boot, uint64_t ns, bool n3ds, BootRead read, void *context)
{
    uint64_t raw[BOOT_MAX];
    bool     overridden;

    memset(boot, 0, sizeof(*boot));
    boot->ns = ns;
    boot->n3ds = n3ds;
    memset(raw, 0, sizeof(raw));
    if (!read(context, ns, raw, &overridden))
        return;
    boot->nsCount = Boot_ListDeps(raw, n3ds, boot->nsDeps);
    boot->complete = Merge(boot->list, &boot->listed, boot->nsDeps, boot->nsCount);

    // Each module in pm's order: the FIRM modules are running; every other one starts and
    // adds its own dependencies.
    for (int i = 0; boot->complete && i < boot->listed; i++)
    {
        if (Boot_IsFirmModule(boot->list[i]))
            continue;
        BootModule *module = &boot->modules[boot->moduleCount++];
        module->id = boot->list[i];
        memset(raw, 0, sizeof(raw));
        if (!read(context, module->id, raw, &module->overridden))
        {
            module->count = -1;
            boot->unread++;
            continue;
        }
        while (module->rawCount < BOOT_MAX && raw[module->rawCount] != 0)
            module->rawCount++;
        module->count = Boot_ListDeps(raw, n3ds, module->deps);
        boot->complete = Merge(boot->list, &boot->listed, module->deps, module->count);
    }
}

void Boot_Try(const Boot *boot, uint64_t host, uint64_t insert, BootTry *result)
{
    uint64_t list[BOOT_MAX], deps[BOOT_MAX + 1];
    int      listed = 0;
    bool     lastStarts = true;     // the last start pm tries works: its result is NS's (NS itself, if none)

    memset(result, 0, sizeof(*result));
    result->at = -1;
    if (!boot->complete)
        return;
    bool fits = Merge(list, &listed, boot->nsDeps, boot->nsCount);
    for (int i = 0; fits && i < listed; i++)
    {
        uint64_t id = list[i];
        if (Boot_IsFirmModule(id))
            continue;
        lastStarts = false;
        if (insert != 0 && id == insert)
        {
            result->at = i;     // assumed not to start: nothing of it is added
            continue;
        }
        const BootModule *module = Find(boot, id);
        if (module == NULL || module->count < 0)
            continue;           // can't be loaded: pm skips it
        lastStarts = true;
        result->lastStarted = id;
        if (result->at >= 0)
            result->startsAfter++;
        if (insert != 0 && id == host)
        {
            deps[0] = insert;
            memcpy(deps + 1, module->deps, (size_t)module->count * sizeof(deps[0]));
            fits = Merge(list, &listed, deps, module->count + 1);
        }
        else
            fits = Merge(list, &listed, module->deps, module->count);
    }
    result->listed = listed;
    result->ok = fits && lastStarts;
    if (insert != 0)
        result->ok = result->ok && result->at >= 0 && result->startsAfter > 0 && listed <= BOOT_ROOM;
}

bool Boot_Choose(const Boot *boot, uint64_t tracker, uint64_t avoid, uint64_t *host, BootTry *result)
{
    bool    found = false;
    BootTry best;

    if (!boot->complete || boot->unread > 0)
        return false;   // the boot isn't known in full: no guessing
    for (int i = 0; i < boot->moduleCount; i++)
    {
        const BootModule *module = &boot->modules[i];
        if ((uint32_t)(module->id >> 32) != SYSMODULE_HIGH || module->id == avoid || module->id == tracker
            || module->overridden || module->count < 0 || module->rawCount >= BOOT_MAX)
            continue;
        BootTry attempt;
        Boot_Try(boot, module->id, tracker, &attempt);
        if (attempt.ok && (!found || attempt.startsAfter > best.startsAfter))
        {
            found = true;
            best = attempt;
            *host = module->id;
        }
    }
    if (found)
        *result = best;
    return found;
}

uint64_t Boot_FindHost(const Boot *boot, uint64_t id)
{
    for (int i = 0; i < boot->moduleCount; i++)
        for (int j = 0; j < boot->modules[i].count; j++)
            if (boot->modules[i].deps[j] == id)
                return boot->modules[i].id;
    return 0;
}
