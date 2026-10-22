#ifndef ACHIEVEMENT_HPP
#define ACHIEVEMENT_HPP

// Drop-in achievement system for CTRPluginFramework plugins.
//
// To add it to another plugin, copy achievement.hpp and achievement.cpp into
// that plugin's sources and make two calls from its main():
//
//     Achievements::Init();
//     *menu += Achievements::Update;   // before menu->Run()
//
// Files on the SD card, where <TitleID> is 16 uppercase hex digits
// (e.g. 0004000000055D00):
//
//     /achievements/<TitleID>.txt            definitions, written by hand
//     /achievements/<TitleID>_unlocked.txt   progress, one unlocked ID per line
//
// Definitions format, one achievement per line ('#' starts a comment line).
// Address and expected value accept hex (0x...) or decimal. Size is 1, 2 or 4
// bytes. The value must equal expected, or with a ">=" prefix be at least
// expected (for counters). The name is the rest of the line and may contain
// spaces.
//
//     # id             address     size expected  name
//     BEAT_SHREDDER    0x0812ABCD  1    1         Beat Shredder
//     TEN_LEVELS       0x0812AC20  1    >=10      Clear 10 Levels
//
// Several lines with the same ID are alternative conditions for one
// achievement (e.g. one per save slot): whichever matches first unlocks it.

namespace Achievements
{
    // Loads this game's definitions and saved progress. Returns false, and
    // leaves Update() doing nothing, if the game has no definitions file.
    bool    Init(void);

    // Checks every locked achievement once. Register it as a PluginMenu
    // callback so it runs once per game frame while the menu is closed.
    void    Update(void);
}

#endif
