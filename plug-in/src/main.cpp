#include <CTRPluginFramework.hpp>

#include "achievement.hpp"

#ifdef DEBUG
#include "devtools.hpp"
#endif

namespace CTRPluginFramework
{
    // Runs once, before the game's first instruction.
    void PatchProcess(FwkSettings &settings)
    {
        // Luma loads only one plugin per game, so keep the Action Replay engine
        // on for players who would otherwise need a separate cheat plugin.
        settings.AllowActionReplay = true;
    }

    // Runs on the plugin thread once the game has started.
    int main(void)
    {
        PluginMenu *menu = new PluginMenu("3DS Achievements", 0, 1, 0,
                                          "Unlocks achievements defined in /achievements/<TitleID>.txt");

        // Run callbacks once per game frame, so Update's frame threshold counts frames.
        menu->SynchronizeWithFrame(true);

        Achievements::Init();
        *menu += Achievements::Update;

#ifdef DEBUG
        DevTools::AddMenuEntries(*menu);
#endif

        // Blocks until the game exits.
        menu->Run();

        delete menu;
        return 0;
    }
}
