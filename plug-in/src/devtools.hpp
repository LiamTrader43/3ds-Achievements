#ifndef DEVTOOLS_HPP
#define DEVTOOLS_HPP

#include <CTRPluginFramework.hpp>

// Tools for finding achievement addresses. Not part of the achievement library;
// main.cpp only adds them to debug builds.
namespace DevTools
{
    // Adds "Dump memory snapshot", which writes every writable game memory
    // region to /achievements/dumps/snap<N>_<address>.bin, plus an index file
    // snap<N>.txt. Compare snapshots on a PC with tools/snapdiff.py.
    void    AddMenuEntries(CTRPluginFramework::PluginMenu &menu);
}

#endif
