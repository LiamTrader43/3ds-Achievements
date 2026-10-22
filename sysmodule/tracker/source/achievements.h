#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

// Achievement logic for the background tracker, reading:
//
//     /achievements/<TitleID>.txt            definitions
//     /achievements/<TitleID>_unlocked.txt   progress, one unlocked ID per line
//
// Definitions: "<id> <address> <size> <expected> <name> | <description>", where
// expected may start with ">=", the description is optional, and lines sharing
// an ID are alternative conditions for one achievement.

#include <stdbool.h>
#include <stdint.h>

// Implemented by the platform (main.c).
bool    Platform_ReadMemory(uint32_t address, uint8_t size, uint32_t *value);
bool    Platform_ReadFile(const char *path, char *buffer, uint32_t capacity, uint32_t *length);
bool    Platform_AppendFile(const char *path, const char *text);
void    Platform_Unlocked(const char *name, const char *description);

// Loads the game's definitions and progress. Returns false, leaving
// Achievements_Update() a no-op, if the game has no definitions.
bool    Achievements_Load(uint64_t titleId);

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

#endif
