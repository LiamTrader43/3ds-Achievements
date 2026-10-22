#ifndef DEVTOOLS_HPP
#define DEVTOOLS_HPP

#include <CTRPluginFramework.hpp>

// Tools for finding achievement addresses. Not part of the achievement library;
// main.cpp only wires them up in debug builds.
namespace DevTools
{
    // Writes every writable game memory region to
    // /achievements/dumps/snap<N>_<address>.bin, plus an index file
    // snap<N>.txt, then notifies. Call it while the game is paused so the
    // snapshot is consistent. Compare snapshots on a PC with tools/snapdiff.py.
    void    DumpSnapshot(void);
}

#endif
