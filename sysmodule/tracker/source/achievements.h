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

#include <stdbool.h>
#include <stdint.h>

// Implemented by the platform (main.c).
bool    Platform_ReadMemory(uint32_t address, uint8_t size, uint32_t *value);
bool    Platform_ReadFile(const char *path, char *buffer, uint32_t capacity, uint32_t *length);
bool    Platform_AppendFile(const char *path, const char *text);
void    Platform_Unlocked(const char *name, const char *description);

// Loads the game's definitions and progress. Returns false, leaving
// Achievements_Update() a no-op, if the game has no definitions. For another
// release of a game (a version.txt), loads that game's definitions with this
// release's address shifts, and its progress.
bool    Achievements_Load(uint64_t titleId);

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
// the next Achievements_Load().
int     Achievements_List(const char **names, bool *unlocked, int capacity);

// Each achievement's description, in the same order as Achievements_List()
// ("" when the line has none). Used by the achievement viewer app.
int     Achievements_Descriptions(const char **descriptions, int capacity);

// Each achievement's points, in the same order as Achievements_List().
int     Achievements_Points(uint16_t *points, int capacity);

// Points of the unlocked achievements, and of all of them.
void    Achievements_Score(int *earned, int *total);

#endif
