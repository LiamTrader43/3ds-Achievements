#ifndef ACHIEVEMENTS_MENU_HPP
#define ACHIEVEMENTS_MENU_HPP

// Achievements list on the bottom screen, replacing CTRPF's default menu.
//
//     menu->OnOpening = AchievementsMenu::Open;   // before menu->Run()
//
// The menu hotkey (Select by default) pauses the game and shows the list on a
// black bottom screen; B or Select closes it. CTRPF's own menu never opens, so
// plugins with their own menu should leave this module out.
namespace AchievementsMenu
{
    // Pauses the game and runs the menu until the player closes it. Always
    // returns false, which keeps CTRPF's menu closed.
    bool    Open(void);

    // Optional action for the X button, run while the menu is open and the
    // game is paused. Unset by default, so X does nothing.
    extern void (*OnXPressed)(void);
}

#endif
