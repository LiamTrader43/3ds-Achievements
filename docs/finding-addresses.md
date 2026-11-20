# Finding memory addresses for 3DS achievements

This guide is for anyone who wants to write achievements for a game that 3DS Achievements doesn't support yet. You don't need to be a programmer, but you'll be reading hex numbers and typing a few commands. Be patient: the first address in a new game can take an evening. After that it goes much faster, because most of what you learn about a game's memory helps with the next achievement.

At the end you'll have a `definitions.txt` like the ones in [`achievements/`](https://github.com/LiamTrader43/3ds-Achievements/tree/main/achievements), tested on your own 3DS and ready to send in.

1. [How the tracker sees a game](#1-how-the-tracker-sees-a-game)
2. [Tools](#2-tools)
3. [Plan your list](#3-plan-your-list)
4. [Look for existing research first](#4-look-for-existing-research-first)
5. [Reading 3DS memory with gdb](#5-reading-3ds-memory-with-gdb)
6. [Way 1: saved progress](#6-way-1-saved-progress)
7. [Way 2: numbers you can see](#7-way-2-numbers-you-can-see)
8. [Way 3: hidden flags](#8-way-3-hidden-flags)
9. [Things that move: pointers](#9-things-that-move-pointers)
10. [Make sure an address is reliable](#10-make-sure-an-address-is-reliable)
11. [Writing the lines](#11-writing-the-lines)
12. [Testing on your 3DS](#12-testing-on-your-3ds)
13. [Other regions](#13-other-regions)
14. [Sending it in](#14-sending-it-in)
15. [Condition cheat sheet](#15-condition-cheat-sheet)

## 1. How the tracker sees a game

While you play, the tracker looks at the game's memory 10 times a second. Each line of a game's definitions file names a place in memory and the value that means "done". When the value is there, the achievement unlocks, once and for good.

So every achievement comes down to three things:

- an **address**, the place in memory, like `0x0058799A`;
- a **size**, how many bytes the value takes: 1, 2 or 4;
- the **value** that means "done", like `>=320`.

Finding addresses is the hard part, and it's what most of this guide is about.

### What you need to know about 3DS memory

**Addresses are written in hexadecimal.** `0x` means a number is in base 16, with the digits 0-9 and A-F. Windows Calculator in Programmer mode adds, subtracts and converts hex numbers.

**Numbers are stored backwards.** The 3DS stores the lowest byte first. The 2-byte number 320, which is `0x0140` in hex, is stored as the bytes `40 01`. Keep this in mind whenever you look at raw memory.

**A game's memory has a few big areas:**

| Addresses | What's there | Same address every time? |
|---|---|---|
| `0x00100000` up to a few MB later | the game's program, then its global variables | Yes, always (for one version of the game). The best place for an address. |
| `0x08000000` and up | the main heap: things the game creates as it runs, like levels, enemies and menus | Usually, not always |
| `0x14000000`-`0x18000000` (older games) or `0x30000000`-`0x34000000` (newer games) | the "linear" heap: graphics, sound and big buffers. Some games keep their save data here. | Usually, not always |

Something on a heap is wherever the game happened to put it. That's usually the same place, but not always: Ocarina of Time 3D puts its main game state somewhere else on the very first launch, before it has a save. [Section 9](#9-things-that-move-pointers) shows how to handle that.

**Every region and version of a game is different.** The European and Japanese releases, or the same game with an update installed, usually keep things at other addresses. Write down which version you're working on.

**Memory that can't be read counts as 0.** If an address isn't in use right now (say, the level it belongs to isn't loaded), the tracker reads 0. Never make 0 mean "done".

**Quick moments can be missed.** The tracker looks every 0.1 seconds, and a simple line has to match three looks in a row. A value that's only right for a moment won't count. Look for something that stays.

## 2. Tools

All of these are free.

- **A 3DS with Luma3DS and 3DS Achievements installed**, for the final test. The tracker only runs on a real console, not in an emulator.
- **Your own copy of the game**, dumped from your console, and **[Azahar](https://azahar-emu.org/)**, a 3DS emulator for your PC. Searching memory is much easier in an emulator, and the addresses found in Azahar have matched the real console so far. [3ds.hacks.guide](https://3ds.hacks.guide/) explains how to dump your games and cartridges with GodMode9.
- **[Cheat Engine](https://www.cheatengine.org/)** (Windows), to search Azahar's memory for numbers. Its installer offers extra software; decline it.
- **[HxD](https://mh-nexus.de/en/hxd/)**, a hex editor, to compare save files and search memory dumps.
- **gdb**, to read memory at the addresses the 3DS uses. It comes with [devkitPro](https://devkitpro.org/): install devkitARM, and the program is called `arm-none-eabi-gdb`. On Linux, `gdb-multiarch` works too.
- **Python 3**, to run `tools/snapdiff.py` from the [3DS Achievements repository](https://github.com/LiamTrader43/3ds-Achievements), which compares memory dumps and finds pointers.

Optional: [Checkpoint](https://github.com/BernardoGiordano/Checkpoint) to copy saves to and from your 3DS, and [FBI](https://github.com/Steveice10/FBI) to see a game's title ID.

## 3. Plan your list

Write down the achievements you want before you look for anything: an ID, a short name, a one-line description and points. Points go from 0 to 1000 and default to 10. Then sort the list by how hard each will be to find:

1. **Saved progress:** beat a level, get an item, finish the story. The game writes these to its save file, which makes them the easiest. Start here.
2. **Counters:** coins, lives, kills, collectibles. A number you can see on screen is easy to search for.
3. **Moments:** defeat a boss, catch a rare fish, see a secret. These are often never saved, or saved later. Medium.
4. **Challenges:** beat a boss without getting hit, finish under a time limit, never jump. These need several addresses and the condition language in [section 11](#11-writing-the-lines). Save them for last.

A good first set is mostly 1 and 2.

## 4. Look for existing research first

Someone may already have found what you need. A few minutes of searching can save hours.

**Cheat codes.** Action Replay and Gateway codes for 3DS games are easy to find, for example in the [CTRPF-AR-CHEAT-CODES](https://github.com/iSharingan/CTRPF-AR-CHEAT-CODES) collection, and each one contains an address. In a code line like `10587A40 00000064`:

- the first digit is the type: `0` writes 4 bytes, `1` writes 2 bytes, `2` writes 1 byte. That's your size.
- the other seven digits are the address: `0x00587A40`.
- if there's a `D3000000 XXXXXXXX` line before it, add `XXXXXXXX` to the address. So `D3000000 14000000` followed by `22197A30 00000005` is the 1-byte value at `0x14000000 + 0x02197A30 = 0x16197A30`.
- a line starting with `B` means the address is behind a pointer. See [section 9](#9-things-that-move-pointers).

**Randomizers, decompilations and modding communities.** If a game has a randomizer, a decompilation or a speedrunning community, its memory may be documented. Most of Ocarina of Time 3D's definitions came from the OoT3D randomizer's source code and a community RAM map.

**The original game.** Many 3DS games are remakes. If the original (N64, GameCube, DS...) has a set on [RetroAchievements](https://retroachievements.org/), you can base your list on it: the names and descriptions are already written, and its conditions tell you what kind of value to look for. The addresses always have to be found again for the 3DS version. Ocarina of Time 3D's 97 achievements are a port of the N64 set this way.

**Wikis.** [The Cutting Room Floor](https://tcrf.net/) and [Data Crystal](https://datacrystal.tcrf.net/) sometimes list memory addresses.

Whatever you find, check it yourself ([section 10](#10-make-sure-an-address-is-reliable)). Something written for another region or version won't match yours.

## 5. Reading 3DS memory with gdb

Cheat Engine ([section 7](#7-way-2-numbers-you-can-see)) shows where a value sits in your PC's memory, and that's a different address from the one the 3DS uses. Azahar has a debugger connection, a "GDB stub", that reads memory at real 3DS addresses. You'll use it to look at values, dump memory to files and check addresses.

### Set up (once)

In Azahar, open **Emulation → Configure → Debug**, tick **Enable GDB Stub**, and leave the port at **24689**.

While it's on, every game waits on a black screen until gdb connects. Turn it off when you're done.

### Each time

1. Start the game in Azahar. It waits.
2. Open a terminal and start gdb:

   ```
   arm-none-eabi-gdb
   ```

3. Type these into gdb:

   ```
   set architecture arm
   set remotetimeout 60
   target remote 127.0.0.1:24689
   continue &
   ```

   The game starts, and you can keep typing gdb commands while it runs.

4. To read memory, pause the game, read, and let it continue:

   ```
   interrupt
   x/16xb 0x0058799A
   continue &
   ```

Commands to use while the game is paused:

| Command | What it does |
|---|---|
| `x/16xb 0x0058799A` | shows 16 bytes from that address, in hex |
| `x/1uh 0x0058799A` | shows one 2-byte number, in decimal |
| `x/1uw 0x0058799A` | shows one 4-byte number, in decimal |
| `x/1xw 0x0058B2E0` | shows one 4-byte number in hex (good for pointers) |
| `dump binary memory snap0_00100000.bin 0x00100000 0x005C7000` | saves that range of memory to a file |

### Things to know

- **Connect once per Azahar session**, right after starting the game, and keep gdb open. If gdb disconnects or you close it, close Azahar completely and start again. Stopping the game isn't enough.
- **If the game stays frozen after `continue &`,** close Azahar and gdb and start over. It happens now and then.
- **A dump fails with "Cannot access memory" if the range includes memory the game isn't using.** Each area from the table in section 1 ends somewhere different in every game. Find where it ends once: `x/1xw 0x00600000` fails if that's past the end, so try addresses until you know the last one that works. Ends are always multiples of `0x1000`. Ocarina of Time 3D's own data, for example, runs from `0x00100000` to `0x005C7000`.
- **Name your dumps `snap<number>_<start address>.bin`,** for example `snap0_00100000.bin`, `snap0_14000000.bin`, `snap1_00100000.bin`. All dumps taken at the same moment share a number. `tools/snapdiff.py` reads these names.
- Each dump takes a few seconds; about 5 seconds for 60 MB.

Luma's Rosalina menu on the 3DS (L + Down + Select) also has a debugger that gdb can connect to over Wi-Fi. It's slower, but it's handy for checking a single address on the console. This guide uses Azahar.

## 6. Way 1: saved progress

Most games keep a copy of the save file in memory, laid out exactly like the file. Find that copy and every value in the save gets an address: **the copy's address + the value's position in the file**. Most of Ocarina of Time 3D was done this way: its save copy is at `0x00587958`, so the maximum health, at position `0x42` in the save file, is at `0x0058799A`.

### Find what changes in the save

1. In Azahar, right-click the game and choose **Open Save Data Location**. That folder holds the save files.
2. Copy the save file to a folder called `1-before`.
3. In the game, do the thing (get the item, beat the level), then save.
4. Copy the save file again, to `2-after`.
5. Open both in HxD and compare them (HxD's compare function is in the Analysis menu). HxD jumps from one difference to the next.
6. Some bytes change on every save: a checksum, the date, the play time. Save once more without doing anything new and compare again, so you know which ones those are.

What's left are candidates. A byte going from `00` to `01` is usually a flag. A byte gaining one bit (`21` to `25`) is also a flag, inside a byte shared by several. A number going up is a counter. Getting a heart container in Ocarina of Time 3D changes position `0x42` from `30 00` to `40 00`: maximum health, 16 per heart, going from 3 hearts to 4.

Doing the same thing in a second save file confirms it: the position that changes in both is the one you want.

### Find the save copy in memory

1. In the save file, find a stretch of bytes that's distinctive and rarely changes, such as the player's name, a word like `ZELDAZ` (at position `0x30` in Ocarina of Time 3D's save), or any 16 to 32 bytes that aren't mostly `00`. Note its position in the file.
2. Load that same save in the game. Pause with gdb and dump the game's own data and the heaps ([section 5](#5-reading-3ds-memory-with-gdb)).
3. Open each dump in HxD and search for those bytes (Search → Find, Datatype "Hex-values", or "Text-string" for a word).
4. Where you find it: **save copy's address = dump's start address + position in the dump − position in the save file.** For example, if `ZELDAZ` is at `0x30` in the save, and at `0x487988` in `snap0_00100000.bin`, the save copy starts at `0x00100000 + 0x487988 − 0x30 = 0x00587958`.
5. If it's there more than once, compare more of the save with each copy. The real one matches the file you loaded and changes as you play.

Now every position you found in the save file gives you an address.

### Two things to check

- **When does the copy change?** Some games update it the moment something happens, others only when they save. Look at the address with gdb before and after the event, without saving. If it only changes when the game saves, the achievement unlocks at the next save. That's often fine, but if the game also keeps the value somewhere that updates right away, use that instead.
- **What's in the copy before a file is loaded?** At the title screen or the file select, it can hold a demo save, an empty file or the last file played. Ocarina of Time 3D loads a debug save on its title screen with almost every item, and without a guard, nearly everything would unlock there. [Section 11](#guards-dont-unlock-on-the-title-screen) shows how to guard against this.

## 7. Way 2: numbers you can see

For coins, lives, ammo or anything else shown as a number:

1. Start the game in Azahar and load a save.
2. Open Cheat Engine, click the computer icon at the top left, and choose `azahar.exe`.
3. In **Edit → Settings → Scan Settings**, also tick **MEM_MAPPED**, so it searches all of Azahar's memory.
4. Type the number you see (say 37 coins) into **Value**, set **Value Type** to **2 Bytes**, and click **First Scan**. If nothing turns up later, try again with **4 Bytes** or **Byte**.
5. Change the number in the game (pick up a coin), type the new number, and click **Next Scan**. Repeat until only a few results are left.
6. To check a result, double-click it to add it to the list at the bottom, change its value there, and see if the game shows the new number. If it doesn't, you've found a copy. Try the next result.

If the number on screen isn't the number in memory (hearts, timers and percentages often aren't), start with **Unknown initial value**, then use **Increased value**, **Decreased value** and **Unchanged value** as it goes up, down or stays the same.

### From Cheat Engine's address to the 3DS address

Cheat Engine's addresses are positions in your PC's memory. They change every time you start Azahar, and the tracker can't use them. To get the 3DS address:

1. Pause the game with gdb (`interrupt`), so nothing changes while you work.
2. In Cheat Engine, right-click the result and choose **Browse this memory region**. Your value is at the top left.
3. Write down 16 to 32 bytes, starting a few bytes before your value and avoiding long runs of `00`. Note how many bytes before the value you started.
4. Dump memory with gdb and search the dumps in HxD for those bytes (Datatype "Hex-values").
5. **3DS address = dump's start address + position in the dump + the bytes you started before the value.**
6. Check it with gdb: `x/1uh <address>` should show your number.

If the bytes turn up in more than one place, use a longer stretch.

## 8. Way 3: hidden flags

For things with no number on screen, like a boss defeated, a door opened or an item collected, compare memory from before and after.

1. Get to just before the event. Take a dump of each area, numbered 0. Move around or wait a little, and take another set, numbered 1.
2. Do the event. Take two more sets, numbered 2 and 3, again in different spots.
3. If you can, restart the game, load the save, and take one more set, numbered 4.
4. Put all the dumps in one folder and run:

   ```
   python tools/snapdiff.py dumps --before 0 1 --after 2 3 --check 4 --require-check
   ```

   It lists every byte that had one value in all the "before" dumps and another in all the "after" dumps, and that still had the "after" value after the restart.

5. To see a candidate and the bytes around it in every dump:

   ```
   python tools/snapdiff.py dumps --before 0 1 --after 2 3 --around 0x0058885F
   ```

Dumps of the same area need the same start and end every time.

Expect lots of candidates at first, because memory gets shuffled around as you move through the game. To narrow them down:

- Take the "before" and "after" dumps in the same place if you can.
- Do the event again in a second save file and compare that too. The real flag changes both times.
- If you found the save copy (Way 1), look there first. Most permanent flags end up in the save.
- Look for single bits. A byte going `00 → 04` or `21 → 25` is one bit turning on.

Cheat Engine can do the same thing: First Scan with **Unknown initial value** (Byte), click **Unchanged value** a few times while nothing happens, **Changed value** after the event, then **Unchanged value** a few more times. Turn what's left into 3DS addresses as in section 7.

## 9. Things that move: pointers

Something on a heap can be somewhere else next time: after a restart, in another area, or on the game's first launch. The game itself keeps track of where it put it, with a **pointer**: a 4-byte value that holds the thing's address. When that pointer is one of the game's global variables, it never moves, and the tracker can follow it to the thing.

Ocarina of Time 3D's game state (current level, enemy lists and more) is usually at `0x0871E840`, but at `0x0874A5F0` on the first launch. The current level number is `0x104` bytes into it. The game keeps a pointer to the game state at `0x0058B2E0`, so the definitions read "the 2 bytes at `0x104` past wherever `0x0058B2E0` points".

To find a pointer:

1. Find your value's address in two runs where it moved. Say it's at `0x0871E944` in one and `0x0874A6F4` in the other.
2. In each run, dump all the areas ([section 5](#5-reading-3ds-memory-with-gdb)), giving the runs different numbers, like 0 and 5.
3. Run:

   ```
   python tools/snapdiff.py dumps --pointers-to 0:0x0871E944 5:0x0874A6F4
   ```

   It lists the 4-byte values that point at your value, or a little before it (to the start of the thing it belongs to), the same way in both runs:

   ```
   location    offset   points to
   0x0058B2E0  +0x104   0x0871E840 0x0874A5F0
   ```

4. Pick one whose location is in the game's own data (an address below `0x08000000`). Its location is the pointer, and the offset is how far past where it points your value is.

If every pointer it finds is itself on a heap, run it again for that pointer's location to find a pointer to the pointer. That's a chain, and the tracker can follow those too. A cheat code line starting with `B` tells you the same thing: `B058B2E0 00000000` followed by `10000104 00000052` means "the 2 bytes at `0x104` past wherever `0x0058B2E0` points".

When the thing isn't loaded, the pointer is often 0 and reading through it gives 0, which is one more reason never to let 0 mean "done". [Section 11](#pointers) shows how to write a pointer in a definition.

## 10. Make sure an address is reliable

Before you write a line, check that the address still holds the right value:

- [ ] after restarting the game
- [ ] after closing and reopening Azahar
- [ ] with a different save file loaded
- [ ] after visiting other areas and coming back
- [ ] on the game's very first launch, before it has a save (in Azahar, move the game's save folder somewhere else for a moment, and put it back afterwards)
- [ ] on your real 3DS ([section 12](#12-testing-on-your-3ds))

If it fails any of these, the value is on a heap and moved. Look for a pointer ([section 9](#9-things-that-move-pointers)), or for the same value somewhere else.

**On a real 3DS, never delete a game's save file to get a first launch.** The HOME Menu can crash when you start the game afterwards. Delete the game and reinstall it instead.

## 11. Writing the lines

A game's achievements go in `achievements/<TitleID>/definitions.txt`. The title ID is 16 hex digits. FBI shows it on the 3DS, and in Azahar it's in the save folder's path: `title/00040000/00033500` means `0004000000033500`.

Start the file with comments (lines beginning with `#`) saying which game, region and version it's for, and what you've learned about the game's memory. Look at [Ocarina of Time 3D's file](https://github.com/LiamTrader43/3ds-Achievements/blob/main/achievements/0004000000033500/definitions.txt) for an example.

### Simple lines

One address, one value:

```
# id          address     size  expected  name | description | points
COINS_100     0x0812ABCD  2     >=100     Rich | Collect 100 coins. | 10
BEAT_LEVEL_1  0x0812AC10  1     2         Off We Go | Beat the first level.
```

- **id:** capital letters, digits and `_`. Players' progress is saved under this ID, so don't change it once people are using your file.
- **address:** in hex, starting with `0x`.
- **size:** 1, 2 or 4 bytes.
- **expected:** an exact value, like `2` or `0x1F`, or `>=` a value, for counters.
- **name, description and points** are optional.

Several lines with the same ID are alternatives: whichever matches first unlocks the achievement. Use this when a game keeps all its save files in memory at once and you have one address per file.

A simple line compares the whole value. If a byte holds several flags, say `0x25` (bits 0, 2 and 5 set), a simple line can't check one of them. Use a condition line instead.

### Condition lines

For single bits, guards, pointers, and anything that needs more than one address, write a condition after `rc:`. The language comes from [RetroAchievements](https://docs.retroachievements.org/developer-docs/condition-syntax.html), and there's a [cheat sheet](#15-condition-cheat-sheet) at the end of this guide. The condition can't contain spaces.

```
BIGGORON_SWORD  rc:0xX588e3c=0_d0xX588e3c=0_0xX588e34<3_0xO587a0e=1_0xH5879aa=1  Biggoron's Sword | Obtain the Biggoron's Sword | 10
```

The parts are separated by `_`, and all of them have to be true at the same time. Each address starts with a letter for its size: `0xH` is 1 byte, `0x` with no letter is 2 bytes, `0xX` is 4 bytes, and `0xM` to `0xT` are bits 0 to 7 of a byte. Numbers are decimal. Write hex numbers with an `h` in front (`h1F`), because `0x1F` would mean "the 2 bytes at address `0x1F`".

So the line above means: the game mode (4 bytes at `0x588E3C`) is 0, and it was 0 on the last look too (`d` means the value 0.1 seconds ago); the file number is below 3; bit 2 of the byte at `0x587A0E` is set (the Giant's Knife or Biggoron's Sword is owned); and the byte at `0x5879AA` is 1 (it's the Biggoron's Sword).

A condition line unlocks the moment it's true. If it's already true when the tracker starts watching (say you started the tracker in the middle of a game), it first waits for it to be false once. Loading a save where it's already done still unlocks it, because the condition was false on the title screen.

#### Guards: don't unlock on the title screen

Many games have something in memory before you pick a file: a demo, an empty file, the last file played. Find a value that means "a real file is loaded and being played", such as a game mode or the current file number, and put it at the start of every line. Ocarina of Time 3D's guard is `0xX588e3c=0_d0xX588e3c=0_0xX588e34<3`. To test it, start the game and wait on the title screen and the file select for a minute. Nothing should unlock.

#### Pointers

`I:` means "the next address is relative to the pointer stored here". `I:0xX58b2e0_0x000104=82` reads the 4-byte pointer at `0x0058B2E0`, goes `0x104` bytes past where it points, and checks that the 2 bytes there are 82. Chains work too: `I:0xX58b2e0_I:0xX0020c4_0x000000=324`.

#### Moments and challenges

A few more pieces let you catch moments and build challenges:

- **`d`** in front of an address means its value on the previous look. `d0xH1234=0_0xH1234=1` is true at the moment the byte goes from 0 to 1. That catches events the game never saves.
- **`.N.`** after a part means it has to be true on N looks, and the looks add up. `.1.` means "has been true at some point".
- **`R:`** in front of a part resets those counts whenever it's true. That's how a challenge attempt fails.
- **`N:`** in front of a part joins it to the next one, so the next part's count only goes up when both are true.
- **`P:`** in front of a part pauses everything while it's true.

Here's a made-up example, "defeat the boss in level 7 without getting hit":

```
UNTOUCHABLE  rc:N:d0xH0812A000!=7_0xH0812A000=7.1._R:0xH0812A000!=7_R:d0x0812B010>0x0812B010_d0xH0812C020=0_0xH0812C020=1  Untouchable | Defeat the boss without taking damage. | 25
```

Read it part by part: arriving in level 7 (the level number wasn't 7 on the last look and is now) is counted once. Leaving level 7 resets that count, and so does health going down (the health on the last look is higher than now). The achievement unlocks at the moment the boss flag goes from 0 to 1, if the arrival is still counted. So any hit means leaving the level and coming back to try again.

Test every challenge both ways. Do it right: it unlocks. Do it wrong in every way you can think of: it doesn't. One wrong character can make a condition always true.

### Plugins that change the game

Luma can load plugins (`.3gx` files) into a game. Some change it so much that its achievements shouldn't count: a cheat menu, a mod with its own tracks and save, or an "unlock everything" option that writes into the save. A line starting with `@block-plugin` turns the game's achievements off while a plugin whose file path contains the given text is running (capital letters don't matter):

```
@block-plugin cheats
```

- `@block-plugin *` blocks every plugin.
- A file can have up to 8 of these lines, anywhere in it.
- While one applies, the start banner says "achievements off" with the plugin's file name, and R + A + Left explains why instead of showing the list.
- The app's **Patched games** setting doesn't override it.
- Versions 2.0.2 and older skip these lines, so the rest of the file still works there, without the block.

Before you write your list, look up the popular plugins and mods for your game and what they do. A plugin that only adds practice tools or a timer can stay allowed. One that unlocks things or edits the save would give away every achievement that reads those values. The [README](https://github.com/LiamTrader43/3ds-Achievements#readme-plugins) has the same rules.

### Missable achievements

Mark achievements that can be missed for good in a playthrough, so players know to watch for them: a challenge in a boss fight that can't be fought again, a scene that only happens once, an item that stops being available. A line starting with `@missable` lists their IDs:

```
@missable BEAT_BOSS_NO_HIT SECRET_DIALOGUE
```

- A line can list several IDs, and a file can have several of these lines, anywhere in it.
- The app and the R + A + Left menu show "Missable" next to a locked one's points, and in its full view.
- If you're porting a RetroAchievements set, mark the ones it marks as Missable.
- Versions 2.0.2 and older skip these lines.

### Limits

- At most 256 lines per file (comments don't count), and the file must stay under 64 KB. Versions 2.0.2 and older read only the first 128 lines and 32 KB, and their updater doesn't download a bigger file.
- A condition can be up to 1,023 characters, but the tracker has limited memory for conditions, so keep them as short as you can.
- Avoid `&` (bitwise AND) in conditions. Use the bit sizes `0xM` to `0xT` instead.

## 12. Testing on your 3DS

1. Copy your file to `/achievements/<TitleID>/definitions.txt` on the SD card.
2. Open the Achievements app, which starts the tracker, then start the game. After a few seconds, a banner shows how many achievements were found, like "0/12 achievements unlocked". If it says "achievements not supported", the folder name is wrong or no line in the file is valid. If it says "achievements off" with a plugin's name, one of your `@block-plugin` lines matched a plugin that's running.
3. Play. Each unlock shows a banner and is written to `/achievements/tracker.log`.
4. To test an achievement again, delete its ID from `/achievements/<TitleID>/progress.txt`. Deleting the file resets the whole game.
5. Check that nothing unlocks too early: on the title screen, on the file select, and when you load a file that hasn't done it yet.

To test without playing through everything, Luma's cheat menu (L + Down + Select → Cheats) can set values for you using Action Replay codes in `/cheats/<TitleID>.txt`. Afterwards, turn the cheats off and remove from `progress.txt` anything they unlocked.

If a line never unlocks on the 3DS, check the address in Azahar with gdb in the same situation, and make sure the game's region and version are the ones you worked with.

## 13. Other regions

Another region of the same game shares its achievements and progress, so you don't write a second list. Check your addresses in that version: the global variables are sometimes at the same addresses, and things on the heaps often moved by a fixed amount. Then give that title ID a folder with only a `version.txt`, as the [README](https://github.com/LiamTrader43/3ds-Achievements#readme-other-regions) explains. If the differences are more than a fixed shift, that release can't share the list yet.

## 14. Sending it in

1. Fork the repository and add `achievements/<TitleID>/definitions.txt`. If you like, add a background (`hero.bin`) and achievement icons (`trophy.bin`) with the tools described in the [README](https://github.com/LiamTrader43/3ds-Achievements#readme-adding-a-game).
2. Open a pull request to `main`. Say which game, region and version you tested, and on which console.
3. Once it's merged, everyone's app downloads it automatically.

## 15. Condition cheat sheet

**Sizes** (put the address right after the letters):

| Write | Reads |
|---|---|
| `0xH1234` | 1 byte |
| `0x1234` | 2 bytes |
| `0xW1234` | 3 bytes |
| `0xX1234` | 4 bytes |
| `0xM1234` to `0xT1234` | bit 0 to bit 7 of the byte |
| `0xL1234` / `0xU1234` | the lower / upper 4 bits of the byte |
| `0xK1234` | how many bits of the byte are set |

**In front of an address:** `d` = its value on the last look (0.1 s ago), `p` = its value before it last changed.

**Comparisons:** `=` `!=` `<` `<=` `>` `>=`. Compare with a number (`25`, or `h19` in hex) or another address.

**In front of a part:**

| Write | Meaning |
|---|---|
| `R:` | reset all counts while this is true |
| `P:` | pause the whole condition while this is true |
| `N:` | this and the next part must both be true |
| `O:` | this or the next part must be true |
| `A:` / `B:` | add this value to / subtract it from the next part's value |
| `I:` | the next address is relative to the pointer stored here |
| `C:` | add this part's count to the next part's |

**After a part:** `.N.` = has to be true on N looks (they add up, and only reset with `R:`).

**Joining:** `_` between parts (all must be true). `S` starts an alternative group: everything before the first `S` must be true, plus everything in at least one group after it.

The full reference is RetroAchievements' [condition syntax](https://docs.retroachievements.org/developer-docs/condition-syntax.html). One look here is one "frame" there: 0.1 seconds.
