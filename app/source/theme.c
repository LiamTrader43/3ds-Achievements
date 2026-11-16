#include "theme.h"

#include <stdio.h>
#include <string.h>

#ifndef THEME_PATH
#define THEME_PATH  "sdmc:/achievements/theme.txt"
#endif

static const Theme *const THEMES[] = { &THEME_MODERN, &THEME_3DS };

const Theme *g_theme = &THEME_MODERN;

const Theme *Theme_Find(const char *id)
{
    for (size_t i = 0; i < sizeof(THEMES) / sizeof(THEMES[0]); i++)
        if (strcmp(THEMES[i]->id, id) == 0)
            return THEMES[i];
    return NULL;
}

void Theme_Load(void)
{
    char  line[32] = "";
    FILE *file = fopen(THEME_PATH, "r");

    g_theme = &THEME_MODERN;
    if (file != NULL)
    {
        if (fgets(line, sizeof(line), file) != NULL)
        {
            line[strcspn(line, " \t\r\n")] = '\0';
            const Theme *theme = Theme_Find(line);
            if (theme != NULL)
                g_theme = theme;
        }
        fclose(file);
    }
    Draw_SetFont(g_theme->font);
}
