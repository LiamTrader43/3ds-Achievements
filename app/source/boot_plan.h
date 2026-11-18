#ifndef BOOT_PLAN_H
#define BOOT_PLAN_H

// Which system module can start the tracker with the console (the setting "Start with the
// console", autostart.c). Plain C, no 3DS calls, so it can be tested on a PC.
//
// At boot Luma's pm starts NS with every module it depends on, as one flat list: NS's
// dependencies in order, then the new dependencies of each module started, appended as it
// starts (Luma3DS sysmodules/pm/source/launch.c, loadWithDependencies). Modules already
// running (the FIRM modules: sm, fs, pm, loader, pxi) are listed but skipped. A module that
// can't be loaded is skipped too, and pm's result is the last start it tried, so a failure is
// forgotten as soon as anything after it is started. More than 48 modules in the list stops
// the console (pm panics).
//
// Putting the tracker first in one module's dependency list (the "host") starts it right after
// the host, before the host's own new dependencies; nothing else moves. That's safe only if the
// boot still goes on as before when the tracker can't start: at least one module must be
// started after it, and the list must stay within its limit.

#include <stdbool.h>
#include <stdint.h>

#define BOOT_MAX            48      // modules pm can list (it panics past it), and dependencies per module
#define BOOT_ROOM           47      // the most we allow with the tracker: one left for another module doing this
#define BOOT_N3DS_MASK      0xF0000000ULL   // a title ID's New 3DS bits

typedef struct
{
    uint64_t id;
    int      count;                 // its dependencies as pm lists them, or -1: couldn't be read (can't start)
    int      rawCount;              // entries in its exheader's list (48 = no room for another)
    uint64_t deps[BOOT_MAX];
    bool     overridden;            // already replaced by a file on the SD card: not a host
} BootModule;

// Reads module id's dependency list (its exheader's, as Luma's loader gives it: 48 title IDs,
// 0 after the last) and whether something on the SD card replaces it (an exheader.bin or a
// .cxi). False if it can't be read.
typedef bool (*BootRead)(void *context, uint64_t id, uint64_t deps[BOOT_MAX], bool *overridden);

typedef struct
{
    uint64_t   ns;
    bool       n3ds;
    bool       complete;            // NS was read and every module fit in pm's list
    int        unread;              // modules whose dependencies couldn't be read
    int        nsCount;
    uint64_t   nsDeps[BOOT_MAX];    // NS's dependencies, as pm lists them
    int        listed;              // modules in pm's list
    uint64_t   list[BOOT_MAX];      // in the order pm takes them
    int        moduleCount;
    BootModule modules[BOOT_MAX];   // each one that pm starts (not the FIRM modules), in that order
} Boot;

// One way the boot could go with the tracker in a host's list.
typedef struct
{
    bool     ok;                    // safe: the tracker is listed, the list fits, and something starts after it
    int      listed;                // modules in pm's list, with the tracker
    int      at;                    // the tracker's place in it (-1: never listed)
    int      startsAfter;           // modules started after it
    uint64_t lastStarted;           // the last module started (pm's result is its)
} BootTry;

// Lists the boot as pm will do it now (the FIRM modules are running, every other module is
// started). n3ds: on an Old 3DS pm leaves out New 3DS modules.
void    Boot_Read(Boot *boot, uint64_t ns, bool n3ds, BootRead read, void *context);

// The boot with insert first in host's dependencies (host 0: as it is). The tracker is
// assumed not to start, the worst case.
void    Boot_Try(const Boot *boot, uint64_t host, uint64_t insert, BootTry *result);

// The module that should start the tracker: a system module started at boot, with room in its
// list, not replaced by anything on the SD card, not avoid, and safe (Boot_Try); of those, the
// one with the most modules started after the tracker (the safest; the earliest of equals).
// False if there's none.
bool    Boot_Choose(const Boot *boot, uint64_t tracker, uint64_t avoid, uint64_t *host, BootTry *result);

// The module that already lists id among its dependencies, or 0.
uint64_t Boot_FindHost(const Boot *boot, uint64_t id);

// A FIRM module (sm, fs, pm, loader, pxi): running before anything else, never started by pm.
bool    Boot_IsFirmModule(uint64_t id);

// What pm lists from an exheader's 48 dependencies: up to the first 0, New 3DS modules left out
// on an Old 3DS, the New 3DS bits removed. Returns how many.
int     Boot_ListDeps(const uint64_t raw[BOOT_MAX], bool n3ds, uint64_t out[BOOT_MAX]);

#endif
