// The tracker's definitions parser (and the rcheevos condition parser it uses),
// compiled into the app so both count and name achievements exactly the same.
// The app shows names and descriptions in full (A on an achievement), so it keeps them whole.
#define NAME_CAPACITY   128
#define DESCRIPTION_CAPACITY 512
#include "../../sysmodule/tracker/source/achievements.c"
