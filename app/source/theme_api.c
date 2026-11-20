// The ThemeApi a theme add-on gets (theme_api.h): a pointer to each function and global.
#include "theme_api.h"

const ThemeApi g_themeApi =
{
    .version = THEME_API_VERSION,
    .size = sizeof(ThemeApi),
#define THEME_API_POINTER(name) .name = &name,
    THEME_API_FUNCTIONS(THEME_API_POINTER)
    THEME_API_DATA(THEME_API_POINTER)
    THEME_API_ADDED(THEME_API_POINTER)
#undef THEME_API_POINTER
};
