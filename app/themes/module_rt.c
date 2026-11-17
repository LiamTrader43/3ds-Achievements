// What every theme add-on carries of the C library: the memory and string functions the
// compiler may call by itself (copying a struct, clearing an array). The rest of the library
// (printf...) comes from the app through the ThemeApi (theme_module.h). Linked into each
// .theme file; never into the app.
#include <stddef.h>

// Otherwise GCC turns these loops into calls to the functions being defined.
#pragma GCC optimize("no-tree-loop-distribute-patterns")

void *memcpy(void *to, const void *from, size_t n)
{
    unsigned char *d = to;
    const unsigned char *s = from;
    while (n--)
        *d++ = *s++;
    return to;
}

void *memmove(void *to, const void *from, size_t n)
{
    unsigned char *d = to;
    const unsigned char *s = from;
    if (d < s)
        while (n--)
            *d++ = *s++;
    else
        while (n--)
            d[n] = s[n];
    return to;
}

void *memset(void *to, int value, size_t n)
{
    unsigned char *d = to;
    while (n--)
        *d++ = (unsigned char)value;
    return to;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n--; x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
