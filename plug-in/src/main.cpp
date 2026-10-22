#include <CTRPluginFramework.hpp>

#include "achievement.hpp"
#include "achievements_menu.hpp"

#ifdef DEBUG
#include "devtools.hpp"
#endif

// This plugin runs only the achievement system. CTRPF's other features are
// switched off here, not in the library, so plugins that copy
// achievement.hpp/.cpp keep theirs.
namespace CTRPluginFramework
{
    // Runs once, before the game's first instruction.
    void PatchProcess(FwkSettings &settings)
    {
        settings.AllowActionReplay = false;
        settings.AllowSearchEngine = false;
        settings.TryLoadSDSounds = false;
    }

    // Runs on the plugin thread once the game has started.
    int main(void)
    {
        // CTRPF's menu object is still needed: its loop runs the callbacks.
        PluginMenu *menu = new PluginMenu("3DS Achievements");

        // Run callbacks once per game frame, so Update's frame threshold counts frames.
        menu->SynchronizeWithFrame(true);
        menu->ShowWelcomeMessage(false);

        bool *screenshotsEnabled;
        u32  *screenshotHotkey;
        PluginMenu::ScreenshotSettings(&screenshotsEnabled, &screenshotHotkey);
        *screenshotsEnabled = false;

        Achievements::Init();
        *menu += Achievements::Update;

        // The menu hotkey shows our achievements list; CTRPF's menu never opens.
        menu->OnOpening = AchievementsMenu::Open;

#ifdef DEBUG
        // X in the achievements menu takes a memory snapshot for finding addresses.
        AchievementsMenu::OnXPressed = DevTools::DumpSnapshot;
#endif

        // Blocks until the game exits.
        menu->Run();

        delete menu;
        return 0;
    }
}
