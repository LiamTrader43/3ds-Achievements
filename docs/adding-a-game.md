# Adding a game

Create `achievements/<TitleID>/definitions.txt`, using the game's 16-digit title ID in uppercase. Lines starting with `#` are comments.

New to finding addresses? [Finding memory addresses for 3DS achievements](finding-addresses.md) walks through it, from planning a list to testing it on your 3DS.

A simple line unlocks when an address holds a value:

```
# id          address     size expected  name | description | points
FIRST_COMBO   0x0812AC10  4    3         First Combo | Land a three-hit combo. | 25
COINS_100     0x0812ABCD  2    >=100     Rich | Collect 100 coins.
```

Size is 1, 2 or 4 bytes. The name, description and points are optional, and points default to 10. Lines sharing an ID are alternatives, so whichever matches first unlocks it.

For anything more involved, use a RetroAchievements condition string. These can't unlock until the condition has been false at least once:

```
BEAT_LEVEL  rc:0xH1618fd9c=2  Level Clear | Beat the first level. | 10
```

## Plugins

Some Luma plugins change a game so much that its achievements shouldn't count, like a cheat menu or a mod that brings its own save. A line starting with `@block-plugin` turns the game's achievements off while a plugin whose file path contains the given text (any case) is running:

```
@block-plugin cheats
```

`@block-plugin *` blocks every plugin. A game can have several of these lines. The start banner says when one applies, the R + A + Left menu shows why instead of the list, and the **Patched games** setting doesn't override it. Older versions of the app and tracker skip these lines.

## Missable achievements

Some achievements can be missed for good in a playthrough, like a challenge in a boss fight that can't be fought again. A line starting with `@missable` marks them by ID, like RetroAchievements' "Missable" type:

```
@missable BEAT_BOSS_NO_HIT SECRET_DIALOGUE
```

A line can list several IDs, and a file can have several of these lines, anywhere in it. The app and the R + A + Left menu show "Missable" next to a locked one's points, and in its full view. Older versions of the app and tracker skip these lines.

## Achievements done in parts

Some achievements take several steps that can happen in different sessions, like winning on every course. Write the achievement with `@parts` instead of a condition, then a `@part` line for each step, with its own condition and name:

```
ALL_COURSES @parts World Tour | Win on all three courses. | 10
@part ALL_COURSES COURSE_1 rc:<condition> First Course
@part ALL_COURSES COURSE_2 rc:<condition> Second Course
@part ALL_COURSES COURSE_3 0x0812ABCD 1 1 Third Course
```

Each part is saved to progress.txt as soon as it's done (`ALL_COURSES/COURSE_1`), with a small banner ("World Tour: First Course, 1/3"), and the achievement unlocks when the last one is done. Parts aren't achievements of their own: they don't count in the totals or points. The app and the R + A + Left menu show "1/3" next to a locked one's points, and every part in its full view. A part ID can be up to 31 characters, and the achievement's ID, a `/` and the part's ID together up to 47. Older versions of the app and tracker skip both kinds of line, so the achievement doesn't appear in them.

## Measured progress

An achievement that counts up to a number the game keeps in its save, like coins collected or tokens found, can show how far you are: "4,200/5,000" next to its points while it's locked, and "Progress: 4,200 of 5,000" in its full view. Mark the condition that holds the number with `M:` (RetroAchievements' Measured), and the condition that checks the save is loaded with `Q:` (MeasuredIf):

```
COINS_5000  rc:Q:0xX1234=h43445353_M:0xX5678>=5000  Beast Mode | Collect 5,000 coins. | 10
```

`G:` instead of `M:` shows a percentage. A count of several things works too: add them up with `A:` (AddSource) and measure the sum, e.g. `A:0xM1000_A:0xM1001_M:0xM1002>=3` for three flags.

The tracker writes the latest value to `measured.txt` in the game's folder (at most once a minute while the game runs, and when it closes), so the app can show it without the game running. A value of 0 isn't shown, since that's what `Q:` gives while the save isn't loaded. Use it for numbers the game saves: a count within one try, like hits in the current race, would keep showing its last value until the next try. Older versions of the app and tracker unlock these lines the same way and just don't show the progress.

## Other regions

Another region (or release) of a game shares its achievements and progress. Instead of definitions, its folder holds a `version.txt` naming the game's folder:

```
# achievements/0004000000033600/version.txt: Ocarina of Time 3D (EUR)
game 0004000000033500
shift 0x0871E840 0x08724753 +0x92FD0
```

The app shows one tile for the game, and unlocks count whichever version you play. `shift` lines are optional (up to 8): each one moves the addresses in a range for this release, here a block that sits 0x92FD0 higher in the European game. Only addresses written in the definitions move. Offsets added to a pointer the game stores (`I:` in a condition) stay as written. A `version.txt` with any other kind of line isn't used, so a release that needs more than moved addresses isn't tracked with wrong ones.

## Art

A game can also have its own background and achievement icons:

```bash
python tools/hero_art.py background.png <TitleID>
python tools/trophy_art.py <TitleID> unlocked.png locked.png
```

Use a background at least 1.8 times as wide as it is tall: a little past each side of the screen comes into view with the 3D slider up.

Each achievement can also have a picture of its own. Put one per achievement in a folder, named after its ID (`BEAT_BOSS.png`; `.jpg`, `.gif`, `.bmp` and `.webp` work too), then run:

```bash
python tools/achievement_icons.py <TitleID> <folder>
```

This writes `achievements/<TitleID>/icons.bin`, with each picture made 48x48 (one that isn't square is cut to its middle square) and mostly transparent parts see-through. A locked achievement shows its picture greyed and darker, unless the folder also has `BEAT_BOSS.locked.png`. Achievements without a picture keep the game's icon from `trophy_art.py`, or the usual trophy, so a game can have pictures for only some of them. Each picture adds about 4.5 KB. The app and the R + A + Left menu show them; older versions ignore the file.

The art tools need Python 3 with Pillow.

## Contributing

To contribute a game, open a pull request to `main`. Players get it automatically once it's merged.
