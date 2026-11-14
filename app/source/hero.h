#ifndef HERO_H
#define HERO_H

// A game's background, /achievements/<TitleID>/hero.bin (tools/hero_art.py makes it): raw
// BGR8 in the top screen's framebuffer order (column by column from the left, each column
// bottom row first), wider than the screen by HERO_MARGIN columns on each side. With 3D on,
// the background sits behind the screen, so one eye sees it moved sideways: the margins are
// what comes into view at the edge. Files from before 3D are exactly screen-sized.

#define HERO_MARGIN         16                                  // columns past each side of the screen
#define HERO_HEIGHT         240
#define HERO_WIDTH          (400 + 2 * HERO_MARGIN)
#define HERO_BYTES          (HERO_WIDTH * HERO_HEIGHT * 3)      // 311,040
#define HERO_SCREEN_BYTES   (400 * HERO_HEIGHT * 3)             // 288,000: the older, screen-sized file

#endif
