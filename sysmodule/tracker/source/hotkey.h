#ifndef HOTKEY_H
#define HOTKEY_H

// The buttons that open the in-game achievement list: R + A + Left unless the app's settings chose others,
// saved in /achievements/hotkey.txt as their names on one line ("R A LEFT"). Shared by the tracker (which reads
// it when a game starts) and the app (which writes it). Plain C, so it can be tested on a PC.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOTKEY_PATH     "/achievements/hotkey.txt"

// The buttons, as the 3DS's HID_PAD register (and libctru's KEY_*) number them.
#define HOTKEY_A        0x001u
#define HOTKEY_B        0x002u
#define HOTKEY_SELECT   0x004u
#define HOTKEY_START    0x008u
#define HOTKEY_RIGHT    0x010u
#define HOTKEY_LEFT     0x020u
#define HOTKEY_UP       0x040u
#define HOTKEY_DOWN     0x080u
#define HOTKEY_R        0x100u
#define HOTKEY_L        0x200u
#define HOTKEY_X        0x400u
#define HOTKEY_Y        0x800u
#define HOTKEY_BUTTONS  0xFFFu          // the ones a hotkey can use (HOME, ZL/ZR and the C-Stick can't be read there)

#define HOTKEY_DEFAULT  (HOTKEY_R | HOTKEY_A | HOTKEY_LEFT)
#define HOTKEY_LEAST    2               // one button alone would open the menu in the middle of playing
#define HOTKEY_MOST     4

// How many buttons keys holds.
int         Hotkey_Count(uint32_t keys);

// Whether keys is a hotkey the app lets you choose: 2 to 4 of HOTKEY_BUTTONS.
bool        Hotkey_Valid(uint32_t keys);

// hotkey.txt's text: button names separated by spaces, '+' or ',' (any case). 0 if a name is unknown or the
// buttons aren't a valid hotkey.
uint32_t    Hotkey_Parse(const char *text);

// The buttons' names joined by separator, in a fixed order (L, R, A, B, X, Y, Up, Down, Left, Right, Select,
// Start): "R + A + Left" with " + ".
void        Hotkey_Text(uint32_t keys, const char *separator, char *out, size_t size);

// What hotkey.txt holds for keys: "R A LEFT\n".
void        Hotkey_FileText(uint32_t keys, char *out, size_t size);

// Luma's Rosalina menu opens on its own combination: L + Down + Select unless changed in Luma's settings
// (Rosalina's "Change the menu combo", saved in /luma/config.ini). The list mustn't share buttons with it.
#define HOTKEY_ROSALINA_DEFAULT (HOTKEY_L | HOTKEY_DOWN | HOTKEY_SELECT)
#define HOTKEY_LUMA_CONFIG      "/luma/config.ini"

// Whether keys and combo get in each other's way: one holds every button of the other, so holding it opens
// both menus (combo 0 = none). Rosalina's own Flashback menu checks its combination the same way.
bool        Hotkey_Overlaps(uint32_t keys, uint32_t combo);

// A combination as Luma writes it: names joined by '+' ("L+Down+Select"), any case, with Luma's other names
// too (ZL, ZR, Touch, "CStick Up", "CPad Left"...), so the bits can reach past HOTKEY_BUTTONS. 0 if it isn't one.
uint32_t    Hotkey_ParseLuma(const char *text);

// config.ini's text: the rosalina_menu_combo it saves, or 0 if it has none.
uint32_t    Hotkey_LumaConfigCombo(const char *ini);

// Allowed, but they could get in the way while playing (the most serious one first).
typedef enum
{
    HOTKEY_FINE,
    HOTKEY_RESET,           // holds L + R + Start or L + R + Select: some games restart
    HOTKEY_FEW,             // only 2 buttons: a game may have you press them together
    HOTKEY_IN_PLAY,         // only A, B, X, Y, L and R: games hold these together all the time
    HOTKEY_GAME_MENU,       // Start or Select: many games open their own menu with them
} HotkeyWarning;

HotkeyWarning Hotkey_Warning(uint32_t keys);

#endif
