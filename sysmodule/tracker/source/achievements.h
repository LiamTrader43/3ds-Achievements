#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

// Achievement logic for the background tracker, reading:
//
//     /achievements/<TitleID>/definitions.txt   definitions
//     /achievements/<TitleID>/progress.txt      progress, one unlocked ID per line
//     /achievements/<TitleID>/version.txt       instead of the two above: another release
//                                               (region) of the game it names, sharing that
//                                               game's definitions and progress
//
// Definitions: "<id> <address> <size> <expected> <name> | <description>", where
// expected may start with ">=", the description is optional, and lines sharing
// an ID are alternative conditions for one achievement. Or
// "<id> rc:<condition> <name> | <description>" with a RetroAchievements
// condition string, evaluated by rcheevos (AND, ResetIf, hit counts, deltas).
// Either form may end in "| <points>" after the description (default 10).
//
// An achievement done in parts (e.g. "win on all six courses"): "<id> @parts <name> | ...",
// and a line "@part <id> <part> <condition> <name>" per part, with either kind of condition.
// Each part is saved to progress.txt as "<id>/<part>" when done, so parts done in different
// sessions add up; the achievement unlocks once every part is done. Parts aren't achievements:
// lists, counts and points skip them. Older builds skip both kinds of line.

//
// Measured progress: an "rc:" condition with a Measured condition (M:, or G: to show it as a
// percentage) has a target, e.g. "M:0xX1234>=5000". Meant for a number the game keeps in its
// save (coins, tokens, records): the tracker keeps each locked achievement's latest value above 0
// (0 = nothing to measure now, e.g. a MeasuredIf (Q:) that's false while the save isn't loaded)
// and saves it to /achievements/<TitleID>/measured.txt ("<id> <value>" lines), so the app shows
// it without the game running. Older builds parse the same lines and simply don't show it.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Implemented by the platform (main.c).
bool    Platform_ReadMemory(uint32_t address, uint8_t size, uint32_t *value);
bool    Platform_ReadFile(const char *path, char *buffer, uint32_t capacity, uint32_t *length);
bool    Platform_AppendFile(const char *path, const char *text);
// Replaces the file with text (measured.txt). The app never calls anything that writes.
bool    Platform_WriteFile(const char *path, const char *text, uint32_t length);
void    Platform_Unlocked(const char *name, const char *description);
// A part of an achievement was done (already saved): done of total, not the last one (the
// last unlocks the achievement: Platform_Unlocked).
void    Platform_PartDone(const char *name, const char *part, int done, int total);

// Loads the game's definitions and progress. Returns false, leaving
// Achievements_Update() a no-op, if the game has no definitions. For another
// release of a game (a version.txt), loads that game's definitions with this
// release's address shifts, and its progress.
bool    Achievements_Load(uint64_t titleId);

// Frees what Achievements_Load() took (it calls this itself first). Afterwards nothing
// is loaded: Achievements_Count() is 0 and Achievements_Update() a no-op.
void    Achievements_Unload(void);

// The game whose folder the loaded definitions and progress came from: the
// title ID given to Achievements_Load(), or the game its version.txt names.
uint64_t Achievements_GameId(void);

// Checks every locked achievement once. An achievement unlocks after its
// condition holds for several consecutive calls.
void    Achievements_Update(void);

// Number of achievement lines loaded (shared IDs count once per line).
int     Achievements_Count(void);

// Achievements unlocked and in total (lines sharing an ID count once).
void    Achievements_Progress(int *unlocked, int *total);

// Fills names/unlocked with each achievement in file order (lines sharing an
// ID count once). Returns how many were written. The names stay valid until
// the next Achievements_Load() or Achievements_Unload().
int     Achievements_List(const char **names, bool *unlocked, int capacity);

// Each achievement's description, in the same order as Achievements_List()
// ("" when the line has none). Used by the achievement viewer app.
int     Achievements_Descriptions(const char **descriptions, int capacity);

// Each achievement's ID, in the same order as Achievements_List() (the app matches them
// with progress.txt, which lists unlocks in the order they happened).
int     Achievements_Ids(const char **ids, int capacity);

// Each achievement's points, in the same order as Achievements_List().
int     Achievements_Points(uint16_t *points, int capacity);

// Whether each achievement is missable (it can be missed for good in a playthrough, like
// RetroAchievements' "missable" type): "@missable <id> [<id> ...]" lines name them. In the same
// order as Achievements_List().
int     Achievements_Missable(bool *missable, int capacity);

// Points of the unlocked achievements, and of all of them.
void    Achievements_Score(int *earned, int *total);

// For each achievement (Achievements_List order): how many of its parts are done and how many
// it has; 0 and 0 for one that isn't done in parts.
int     Achievements_Parts(uint8_t *done, uint8_t *total, int capacity);

// The parts of the index-th achievement (Achievements_List order), in file order: their names
// and whether each is done. 0 for one that isn't done in parts.
int     Achievements_PartList(int index, const char **names, bool *done, int capacity);

// Measured progress of each achievement (Achievements_List order): its latest value (0 = none
// known yet), its target (0 = not measured) and whether to show it as a percentage.
int     Achievements_Measured(uint32_t *value, uint32_t *target, bool *percent, int capacity);

// The index-th achievement's (Achievements_List order) measured progress as text (see
// Achievements_MeasuredText). False, and out untouched, if it isn't measured, no value is known
// yet or it's unlocked.
bool    Achievements_MeasuredTextAt(int index, bool full, char *out, size_t size);

// Writes measured.txt if a value changed since it was last written. The tracker calls it now and
// then while a game runs, and before the game's definitions are unloaded.
void    Achievements_SaveMeasured(void);

// A measured value as text: "4,200/5,000" (full: "4,200 of 5,000"), or "84%" when percent. The
// value is shown at most as the target.
void    Achievements_MeasuredText(uint32_t value, uint32_t target, bool percent, bool full, char *out,
                                  size_t size);

// "@block-plugin <text>" lines in the loaded definitions: while a Luma plugin whose file path
// contains that text (any case) runs in the game, its achievements are off. "*" blocks any
// plugin. How many there are (0: plugins don't matter for this game).
int     Achievements_PluginBlockCount(void);

// True if the plugin at path is blocked for this game. path NULL: a plugin runs but its path
// is unknown, and only "*" blocks it.
bool    Achievements_PluginBlocked(const char *path);

#endif
