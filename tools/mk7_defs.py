"""Writes achievements/games/0004000000030800/definitions.txt (Mario Kart 7 USA 1.2; the EUR and JPN releases
use it through their version.txt): the achievements read from the save copy in RAM, and from the race.

    python tools/mk7_defs.py [out.txt]

Every condition reads through the save pointer: S = [0x006673C8] + 0x1D64 (checked in Azahar,
2026-11-17), so a field at save offset `off` is AddAddress 0x006673C8, then address 0x1D64 + off.
Every line also needs the save's magic "SSDC" at S on this poll and the last one, so nothing can
match while the game is loading or the pointer is stale.

Version 1.2 only (user, 2026-11-21: "I don't want to support 1.0"): the file's "@version" line makes the
tracker load these achievements only when the game is 1.2; any other version gets "achievements off: needs
version 1.2" on the start banner instead. (In Azahar with the USA 1.0 cart, the save lines did read 1.0
correctly and the race structures looked the same in the title screen's demo races, but 1.0 isn't supported.)
"""
import os
import sys

POINTER = 0x006673C8
BASE = 0x1D64                   # S - [POINTER]
MAGIC = 0x43445353              # "SSDC"
GP = 0x70C                      # u8[32] per cup and class: index cup * 4 + class (to confirm)
COINS = 0x4C28                  # u32 total coins collected
CHARACTERS = 0x4E42             # u16: bit0 Daisy ... bit7 Lakitu, bit8 Mii
BODIES, TIRES, GLIDERS = 0x4E48, 0x4E4C, 0x4E50
SEQUENCE = 0x4E54               # u8: bit0 won every cup, bit4 Mirror unlocked, bit6 watched the credits
BIT = "MNOPQRST"                # rcheevos bit operands 0xM..0xT = bits 0..7
# Version 1.2: a read-only word (it can't change while the game runs). 1.2 inserted 92 bytes of read-only data at
# 0x0064E4B4, moving this constant from 0x0064E4A0 (where every region's 1.0 has it) to 0x0064E4FC; 1.0 has
# 0x00010001 there (USA, EUR and JPN alike). The tracker reads it once, on the game's first poll ("@version").
VERSION_ADDR, VERSION_12 = 0x0064E4FC, 0xD899E1C9

CUPS = ["Mushroom", "Flower", "Star", "Special", "Shell", "Banana", "Leaf", "Lightning"]
CUP_IDS = ["MUSHROOM", "FLOWER", "STAR", "SPECIAL", "SHELL", "BANANA", "LEAF", "LIGHTNING"]

# Time Trials (checked in Azahar 2026-11-18): RankingSaveData at S+0x10C, one 0x30-byte block per course in
# cup order (Toad Circuit 0, Daisy Hills 1, ...), 5 best times then the best lap, 8 bytes each: a 24-bit time
# (bit 0 set, minutes bits 1-7, seconds bits 8-13, milliseconds bits 14-23; empty = 99:59.999), then the
# driver and parts. Block i's best time = bytes b0 b1 b2 at S+0x10C+0x30*i:
#   b0 = 2*min + 1, b1 = sec + 64*(ms & 3), b2 = ms >> 2, so
#   total ms = 30000*b0 + 1000*b1 + 4*b2 - 63999*(bit6 of b1) - 127998*(bit7 of b1) - 30000.
RANKING = 0x10C
# (save id, course, Expert Staff Ghost time in ms) in cup order; the times are the ROM's staff ghosts
# (/replay/replayF<course id>.dat header +4: minutes bits 0-6, seconds 7-13, ms 14-23).
EXPERT = [
    ("TOAD_CIRCUIT", "Toad Circuit", 86632), ("DAISY_HILLS", "Daisy Hills", 111111),
    ("CHEEP_CHEEP_LAGOON", "Cheep Cheep Lagoon", 102011), ("SHY_GUY_BAZAAR", "Shy Guy Bazaar", 134714),
    ("WUHU_LOOP", "Wuhu Loop", 92854), ("MARIO_CIRCUIT", "Mario Circuit", 130018),
    ("MUSIC_PARK", "Music Park", 127511), ("ROCK_ROCK_MOUNTAIN", "Rock Rock Mountain", 139149),
    ("PIRANHA_PLANT_SLIDE", "Piranha Plant Slide", 126540), ("WARIO_SHIPYARD", "Wario Shipyard", 132313),
    ("NEO_BOWSER_CITY", "Neo Bowser City", 118996), ("MAKA_WUHU", "Maka Wuhu", 99361),
    ("DK_JUNGLE", "DK Jungle", 130777), ("ROSALINAS_ICE_WORLD", "Rosalina's Ice World", 133091),
    ("BOWSERS_CASTLE", "Bowser's Castle", 135244), ("RAINBOW_ROAD", "Rainbow Road", 112077),
    ("N64_LUIGI_RACEWAY", "N64 Luigi Raceway", 112206), ("GBA_BOWSER_CASTLE_1", "GBA Bowser Castle 1", 82221),
    ("WII_MUSHROOM_GORGE", "Wii Mushroom Gorge", 110526), ("DS_LUIGIS_MANSION", "DS Luigi's Mansion", 118539),
    ("N64_KOOPA_TROOPA_BEACH", "N64 Koopa Troopa Beach", 102909),
    ("SNES_MARIO_CIRCUIT_2", "SNES Mario Circuit 2", 77777),
    ("WII_COCONUT_MALL", "Wii Coconut Mall", 134167), ("DS_WALUIGI_PINBALL", "DS Waluigi Pinball", 147574),
    ("N64_KALIMARI_DESERT", "N64 Kalimari Desert", 108707), ("DS_DK_PASS", "DS DK Pass", 136534),
    ("GCN_DAISY_CRUISER", "GCN Daisy Cruiser", 103341), ("WII_MAPLE_TREEWAY", "Wii Maple Treeway", 154877),
    ("WII_KOOPA_CAPE", "Wii Koopa Cape", 148585), ("GCN_DINO_DINO_JUNGLE", "GCN Dino Dino Jungle", 134430),
    ("DS_AIRSHIP_FORTRESS", "DS Airship Fortress", 127704), ("SNES_RAINBOW_ROAD", "SNES Rainbow Road", 81994),
]


def field(kind, off):
    """A read of the save field at off: kind is X (u32), "" (u16), H (u8) or a bit letter M..T."""
    return "I:0xX%08x_0x%s%06x" % (POINTER, kind, BASE + off)


def guard():
    return "I:0xX%08x_0xX%06x=h%08x_I:0xX%08x_d0xX%06x=h%08x" % (POINTER, BASE, MAGIC, POINTER, BASE, MAGIC)


def bit(off, n, value=1):
    return "%s=%d" % (field(BIT[n % 8], off + n // 8), value)


def gp_index(cup, cls):
    return GP + cup * 4 + cls


def gold(cup, cls):                 # trophy bits 4-5 = 3
    return [bit(gp_index(cup, cls), 4), bit(gp_index(cup, cls), 5)]


def three_stars(cup, cls):          # rank bits 1-3 = 6
    i = gp_index(cup, cls)
    return [bit(i, 1, 0), bit(i, 2), bit(i, 3)]


def source(flag, kind, off, factor):
    """An AddSource (A) or SubSource (B) of the save field at off, times factor."""
    return "I:0xX%08x_%s:0x%s%06x*%d" % (POINTER, flag, kind, BASE + off, factor)


def faster_than(i, ms):
    """Block i's best Time Trial time is below ms (see RANKING)."""
    b = RANKING + 0x30 * i
    # a course never raced is all zero (no record, bit 0 clear), not 99:59.999
    return [bit(b, 0), source("A", "H", b, 30000), source("A", "H", b + 1, 1000), source("A", "H", b + 2, 4),
            source("B", "S", b + 1, 63999), source("B", "T", b + 1, 127998), "0<%d" % (ms + 30000)]


def clock(ms):
    return "%d:%02d.%03d" % (ms // 60000, ms // 1000 % 60, ms % 1000)


# ---- live race state (checked in Azahar 2026-11-18) ------------------------------------
# Pointer chains from RootSystem (0x006789B8): [ROOT+4] SceneManager -> [+4] GameScene -> [+0x1E4] ^ key
# CharacterEngine -> [+0x1C] DirectorList. Hex without zero padding keeps the lines under the tracker's
# 1023 characters per condition.
DL = "I:0xX6789bc_I:0xX4_I:0xX1e4^h75f1b26b_I:0xX1c_"
RD = DL + "I:0xX1c_"                          # RaceDirector; its CRaceInfo is at +0x2C
MM = RD + "I:0xX1bc_"                         # ModeManager
KI = MM + "I:0xX4cc_I:0xX0_"                  # KartInfo of player 0
VEH = DL + "I:0xX10_I:0xX2c_I:0xX0_I:0xX2c_"  # player 0's Vehicle (KartDirector -> units -> unit 0)
def vehicle(k):
    """Kart k's Vehicle (0 = the player): KartDirector -> units -> unit k -> its Vehicle."""
    return DL + "I:0xX10_I:0xX2c_I:0xX%x_I:0xX2c_" % (4 * k)


SPINY_DIR = DL + "I:0xX38_I:0xX1c_I:0xX10_"         # ItemDirector -> its 22 item-object directors -> [4] = Spiny
SPINY_OBJ = SPINY_DIR + "I:0xX10_I:0xX0_"           # the one Spiny Shell object (the director holds 1)
ITEM0 = DL + "I:0xX38_I:0xXc8_I:0xX0_"           # player 0's KartItem (ItemDirector -> list -> kart 0)


def live(prefix, read, flag="", hits=0):
    """One condition on a live field: prefix = the chain, read = e.g. '0xH48=3'."""
    return "%s%s%s%s" % (prefix, flag, read, ".%d." % hits if hits else "")


STATE = "0xH48"       # MM: race state 0 sync, 1 countdown, 2 race, 3 goal
RANK0 = "0xH1fd"      # MM: player 0's rank
COURSE = "0xX18c"     # RD: CRaceInfo course (race info +0x160)
RULE = "0xX194"       # RD: rule (0 Grand Prix, 1 time trial, 3 battle, 4 award, 5 course preview)
RACE_TYPE = "0xX198"  # RD: race type (2 race, 1 Balloon Battle, 0 Coin Runners)
ENGINE = "0xX19c"     # RD: engine level (0 50cc, 1 100cc, 2 150cc, 3 mirror)
DETAIL = "0x1b0"      # RD: the local player's kart id (u16)
POINTS0 = "0x4c"      # RD: player 0's Grand Prix points (u16, cumulative, set after each goal)
BODY0, TIRE0, GLIDER0 = "0xX2c", "0xX30", "0xX34"   # RD: player 0's parts
RACE_COINS = "0x46"   # KI: coins in this race (u16)
LAP = "0xH58"         # KI: lap 1..3
ACCIDENT = ("0xMc32", "0xNc32")   # VEH: status flags 0x10000 / 0x20000: spun out / knocked over
LAKITU = "0xRc30"     # VEH: status flag 0x20: Lakitu picks the kart up
HIT_ITEM = "0xX117c"   # VEH: what last hit the kart (0 green shell, 1 red shell, 2 banana, 5 Spiny, 6 Lightning,
                       # 9 Bob-omb, 0xD fireball, 0xE leaf tail; 0xFFFFFFFF for a hit with no item); set at
                       # every hit, FFFFFFFF at the start of each race (checked in Azahar 2026-11-19)
HIT_BY = "0xX1180"     # VEH: the kart that used that item (0 = the player); 0xFFFFFFFF with no item
FIRST_PERSON = "0xXe8"  # VEH: 1 while the first-person view is chosen (Up on the D-pad; Down goes back)
SPINY_ACTIVE = "0xX24"   # SPINY_DIR: 1 while a Spiny Shell is out (from the throw to ~40 frames after it blows up)
SPINY_TARGET = "0xX28c"  # SPINY_OBJ: the kart it chases (0 = the player; 0xFFFFFFFF before it picks one)
SPINY_MODE = "0x2c8"     # SPINY_OBJ (16 bits): 0 flying, 1 over its target, 0x102 diving, 0x203 blowing up,
                         # 0x300 done (the u32 adds 0x10000 on the frame a mode starts); checked in Azahar 2026-11-19
HIT_REACTION = "0xH1168"   # VEH: how the kart reacts to its current hit (0x12 = no hit; set on the hit's frame,
                            # with HIT_ITEM and HIT_BY, back to 0x12 when the kart recovers)
STAR_TIME = "0xXff4"        # VEH: frames left of the kart's Star (450 when used, counting down; 0 = no Star)
EQUIPPED = "0xX40"          # ITEM0: the item out (0x100 + item trailing; 0x20E = a Lucky Seven's items around the kart)
DRIFT = "0xHef4"       # VEH: drift state (u8): 1 none, 2 / 4 hopping, 8 / 0x10 drifting left / right, 0x20 the release
BOOST_TYPE = "0xXf98"  # VEH: the boost running (1 mini-turbo or trick, 2 start, 6 dash panel, 7 mushroom, 8 none)
# VEH +0xFA0 + 4 * type: each boost type's timer (s32). A boost sets its own (dash panel 60, mini-turbo 27-29 blue
# or 84-85 red, trick 20, depending on the kart) and it counts down; while a boost runs, every other type's timer
# counts down from 0 too, below zero, and they all jump back to 0 when it ends. Checked frame by frame in the
# recordings 2026-11-20 (mk7mov/ana_boost.py; the one-frame flags at +0xC28 are too short for 10 polls a second).
MT_TIMER = "0xXfa4"    # type 1: mini-turbo or trick
PANEL_TIMER = "0xXfb8"  # type 6: dash panel
MT_COUNT = 30
TOTAL_RANK0 = "0x50"   # RD: player 0's Grand Prix standing (u16, after each goal)
STOCKED = "0xH38"      # ITEM0: the item in the slot, 0xFF when empty (+0x3C uses left; checked in recordings
                       # 2026-11-19: a held Triple Mushroom stayed 7 to the goal in a button-driven race)
# MenuData = [SequenceEngine + 0xC0], SequenceEngine = [[ROOT+0x10] + 0x214] ^ key (root scene engine 4);
# +0x640 + player: gold steering wheel unlocked, worked out by the game at each goal from the handle marks.
MENU = "I:0xX6789c8_I:0xX214^h75f1b26b_I:0xXc0_"


def win_150():
    """Player 0 has just finished first in a 150cc / Mirror Grand Prix race (the goal state lasts through
    the results, so this stays true for a few seconds)."""
    return [live(MM, STATE + "=3"), live(MM, RANK0 + "=1"), live(RD, ENGINE + ">=2"), live(RD, RULE + "=0"),
            live(RD, DETAIL + "=0")]


def race_started_latch():
    """Hit once when the race starts (countdown -> race); reset by the next countdown."""
    return [live(MM, "d" + STATE + "=1", "N:"), live(MM, STATE + "=2", hits=1), live(MM, STATE + "=1", "R:")]


def new_cup_reset():
    """ResetIf: the countdown of a Grand Prix's first race (points still 0; not the course preview)."""
    return [live(RD, RULE + "=0", "N:"), live(MM, STATE + "=1", "N:"), live(RD, POINTS0 + "=0", "R:")]


def boost_set(timer, last, before=()):
    """Two AndNext chains, true on the poll a boost's timer (VEH) was set: it's 1..255 now and on the last poll it was
    lower, or below zero (counting down while another boost ran). A timer only goes up when its boost starts (the
    jump from below zero back to 0 isn't counted: it's 0). `last` is the flag of each chain's last condition ("C:"
    to count, "R:" to reset); `before` are conditions put first in both chains."""
    out = []
    for since in ("d%s<%s" % (timer, timer), "d%s>=h80000000" % timer):
        out += list(before) + [live(VEH, timer + ">=1", "N:"), live(VEH, timer + "<=255", "N:"), live(VEH, since, last)]
    return out


def live_lines():
    out = []
    # Tokyo Drift: a mini-turbo (blue or red) = the mini-turbo timer was set on a poll after one where the kart was
    # drifting (a trick sets the same timer, but never right after a drift: rec_*.bin, 285 mini-turbos in 6
    # recordings, the rule counted 284 at every phase of the 6-frame polling, mk7mov/check_mt.py). Counted with
    # AddHits in any race (Grand Prix, Time Trial, VS), reset by each countdown.
    drifting = [live(MM, STATE + "=2", "N:"), live(VEH, "d" + DRIFT + ">=8", "N:"), live(VEH, "d" + DRIFT + "<=16", "N:")]
    out.append(line("TOKYO_DRIFT", boost_set(MT_TIMER, "C:", drifting)
                    + ["0=1.%d." % MT_COUNT, live(MM, STATE + "=1", "R:"), live(RD, RULE + "<=2"), live(RD, DETAIL + "=0")],
                    "Tokyo Drift", "Pull off %d mini-turbos (blue or red sparks) in one race." % MT_COUNT, 10))
    # No Shortcuts Needed: no dash panel (its timer set, or the running boost a dash panel's) from the start to the goal.
    out.append(line("NO_SHORTCUTS", race_started_latch() + [live(VEH, BOOST_TYPE + "=6", "R:")]
                    + boost_set(PANEL_TIMER, "R:") + win_150(), "No Shortcuts Needed",
                    "Win a 150cc Grand Prix race without touching a dash panel.", 10))
    out.append(line("PERFECT_CUP", [live(MM, STATE + "=3"), live(RD, POINTS0 + "=40"), live(RD, ENGINE + ">=2"),
                                    live(RD, RULE + "=0"), live(RD, DETAIL + "=0")],
                    "Clean Sweep", "Win all four races of a 150cc or Mirror cup.", 10))
    out.append(line("UNTOUCHABLE", race_started_latch() + [live(VEH, a + "=1", "R:") for a in ACCIDENT]
                    + win_150(), "Untouchable",
                    "Win a 150cc Grand Prix race without spinning out or being knocked over.", 10))
    out.append(line("LAST_TO_FIRST", [live(KI, LAP + "=3", "N:"), live(MM, STATE + "=2", "N:"),
                                      live(MM, RANK0 + "=8", hits=1), live(MM, STATE + "=1", "R:")] + win_150(),
                    "Last to First", "Win a 150cc Grand Prix race after being in 8th place on the final lap.", 10))
    out.append(line("NO_LAKITU", race_started_latch() + [live(VEH, LAKITU + "=1", "R:"), live(RD, COURSE + "=13")]
                    + win_150(), "Not Today, Lakitu",
                    "Win Rainbow Road in a 150cc Grand Prix without Lakitu fishing you out.", 10))
    out.append(line("BLUE_YOURSELF", [live(MM, STATE + "=2"), live(RD, RULE + "=0"), live(RD, DETAIL + "=0"),
                                      live(VEH, HIT_ITEM + "=5"), live(VEH, HIT_BY + "=0")],
                    "Blue Yourself", "Get hit by a Spiny Shell you threw yourself.", 5))
    # Out of the Blue: a Spiny Shell starts blowing up on the player (target 0, mode -> 0x203) and goes away (active 1 -> 0)
    # without the player being spun out or knocked over while it was out (a Mushroom at the right moment, a Star...).
    # The latch is taken only on the poll the blast starts, so a spin-out on that poll or any later one (a hit spins the
    # kart on the blast's frame) clears it for good; it only lives from the blast to the Spiny's end, so the resets
    # need no "while it's out".
    out.append(line("SPINY_DODGE", [live(SPINY_OBJ, SPINY_TARGET + "=0", "N:"), live(SPINY_OBJ, "d" + SPINY_MODE + "!=h203", "N:"),
                                    live(SPINY_OBJ, SPINY_MODE + "=h203", hits=1)]
                    + [live(VEH, a + "=1", "R:") for a in ACCIDENT]
                    + [live(MM, STATE + "=1", "R:"), live(SPINY_DIR, "d" + SPINY_ACTIVE + "=1"),
                       live(SPINY_DIR, SPINY_ACTIVE + "=0"), live(MM, STATE + "=2"), live(RD, RULE + "=0"),
                       live(RD, DETAIL + "=0")],
                    "Out of the Blue", "Dodge a Spiny Shell.", 10))
    out.append(line("NO_ITEMS", race_started_latch() + [live(MM, STATE + "=2", "N:"), live(ITEM0, STOCKED + "!=255", "R:")]
                    + win_150(), "Purist", "Win a 150cc Grand Prix race without picking up an item.", 10))
    out.append(line("GOLD_RIDE", win_150() + [live(RD, BODY0 + "=16"), live(RD, TIRE0 + "=6"),
                                              live(RD, GLIDER0 + "=6")],
                    "All That Glitters",
                    "Win a 150cc Grand Prix race in the Gold Standard with Gold Tires and the Gold Glider.", 5))
    out.append(line("TEN_COIN_CUP", [live(MM, "d" + STATE + "=2", "N:"), live(MM, STATE + "=3", "N:"),
                                     live(KI, RACE_COINS + "=10", hits=4),
                                     live(MM, "d" + STATE + "=2", "N:"), live(MM, STATE + "=3", "N:"),
                                     live(KI, RACE_COINS + "<10", "R:")] + new_cup_reset()
                    + [live(RD, ENGINE + ">=2"), live(RD, RULE + "=0")],
                    "Pockets Full", "Finish every race of a 150cc or Mirror cup holding 10 coins.", 10))
    # (ROCKET_CUP, "Off to a Flying Start", removed 2026-11-18 at the user's request: boost type 2 at +0xF98)
    # Balloon Battle: rule 3, race-info type 1 (2 in races), KartInfo +0x56 balloons (3 at the start, refilled
    # after the last one goes), ranked by points (+0x54) when the time runs out (state 3)
    out.append(line("NOT_A_SCRATCH", race_started_latch()
                    + [live(MM, STATE + "=2", "N:"), live(KI, "d0x56>0x56", "R:"),
                       live(MM, STATE + "=3"), live(MM, RANK0 + "=1"), live(RD, RULE + "=3"),
                       live(RD, RACE_TYPE + "=1"), live(RD, DETAIL + "=0")],
                    "Not a Scratch", "Win a Balloon Battle without losing a balloon.", 10))

    # Battle tours, done in parts (the tracker's "@parts"): a win on each battle course is saved as it
    # happens, so wins in different sessions add up. A win: the time runs out (state 3) with the player first.
    battle = [(0x20, "WUHU_TOWN", "Wuhu Town"), (0x21, "HONEYBEE_HIVE", "Honeybee Hive"),
              (0x22, "SHERBET_RINK", "Sherbet Rink"), (0x23, "DS_PALM_SHORE", "DS Palm Shore"),
              (0x24, "N64_BIG_DONUT", "N64 Big Donut"), (0x25, "GBA_BATTLE_COURSE_1", "GBA Battle Course 1")]
    for tour, name, description, kind in (("BALLOON_TOUR", "Pop Tour", "Win a Balloon Battle on all six battle courses.", 1),
                                          ("COIN_TOUR", "Coin Tour", "Win Coin Runners on all six battle courses.", 0)):
        out.append(parts_line(tour, name, description, 10))
        for course, cid, cname in battle:
            out.append(part_line(tour, cid, [live(MM, STATE + "=3"), live(MM, RANK0 + "=1"), live(RD, RULE + "=3"),
                                             live(RD, RACE_TYPE + "=%d" % kind), live(RD, COURSE + "=%d" % course),
                                             live(RD, DETAIL + "=0")], cname))

    out += item_hit_lines()

    out.append(line("FP_RACE", [live(MM, "d" + STATE + "=1", "N:"), live(MM, STATE + "=2", "N:"),
                                live(VEH, FIRST_PERSON + "=1", hits=1),
                                live(MM, STATE + "=2", "N:"), live(VEH, FIRST_PERSON + "=0", "R:"),
                                live(MM, STATE + "=1", "R:")] + win_150(),
                    "Eyes on the Road", "Win a 150cc Grand Prix race in first-person view the whole race.", 10))
    out.append(line("FP_CUP", [live(RD, RULE + "=0", "N:"), live(MM, "d" + STATE + "=2", "N:"),
                               live(MM, STATE + "=3", hits=4),
                               live(MM, STATE + "=2", "N:"), live(VEH, FIRST_PERSON + "=0", "R:")] + new_cup_reset()
                    + [live(MM, STATE + "=3"), live(RD, TOTAL_RANK0 + "=1"), live(RD, ENGINE + ">=2"),
                       live(RD, RULE + "=0")],
                    "Through My Eyes", "Win a 150cc or Mirror cup driving in first-person view in every race.", 25))
    out.append(line("GOLD_WHEEL", [live(MENU, "0xH640=1")], "Golden Grip",
                    "Earn the Gold Steering Wheel (race with the gyro in at least 80% of your last 100 races).", 10))
    return out


def racers_hit(item=None):
    """3 different karts hit by the player since the last reset: each kart (1-7) counts once (AddHits, a hit target
    of 1 each), when its reaction changes from "no hit" with the player as the hit's kart (and item, if given)."""
    out = []
    for k in range(1, 8):
        v = vehicle(k)
        out += [live(v, "d" + HIT_REACTION + "=18", "N:"), live(v, HIT_REACTION + "!=18", "N:")]
        if item is not None:
            out.append(live(v, HIT_ITEM + "=%d" % item, "N:"))
        out.append(live(v, HIT_BY + "=0", "C:", hits=1))
    return out + ["0=1.3."]


def item_hit_lines():
    """Hits on other racers in one go (checked in Azahar 2026-11-19 with the player as a CPU given each item: every
    kart's Vehicle records the item that hit it (HIT_ITEM: 4 Star, 0xD fireball, 0 / 1 shells, 2 banana, 9 Bob-omb)
    and the kart that used it (HIT_BY), mk7mov/watch_items.py, rec_items1.bin)."""
    out = []
    race = [live(MM, STATE + "=1", "R:"), live(MM, STATE + "=2"), live(RD, RULE + "=0"), live(RD, DETAIL + "=0")]
    # A Star: only while its time runs (VEH +0xFF4); its hits are item 4.
    out.append(line("STAR_HITS", [live(VEH, STAR_TIME + "=0", "R:")] + race + racers_hit(4),
                    "Starstruck", "Hit 3 racers with a single Star.", 5))
    # A Fire Flower: the slot shows it (12) while it lasts (~11 s); its fireballs are item 0xD.
    out.append(line("FIRE_HITS", [live(ITEM0, STOCKED + "!=12", "R:")] + race + racers_hit(0xD),
                    "Playing with Fire", "Hit 3 racers with a single Fire Flower.", 5))
    # A Lucky Seven: its items are out from when it's used (+0x40 = 0x20E, the count at +0x44 going 7 -> 0) and
    # can hit after the last one is thrown, so its hits count until the player gets another item (the slot fills
    # again: reset). Any item of the player's counts (its seven are different items).
    out.append(line("LUCKY_SEVEN", [live(ITEM0, STOCKED + "!=255", "R:"), live(ITEM0, EQUIPPED + "=h20e", hits=1)]
                    + race + racers_hit(), "Jackpot", "Hit 3 racers with items from a single Lucky Seven.", 10))
    # A Super Leaf's tail hitting a racer is item 0xE (kart 5 in rec_items1.bin). One racer is enough; outside a
    # Grand Prix the count resets (user, 2026-11-20: this instead of knocking away an item, which no kart records).
    out.append(line("LEAF_BLOCK", [live(RD, RULE + "!=0", "R:"), live(MM, STATE + "=2"), live(RD, DETAIL + "=0")]
                    + racers_hit(0xE)[:-1] + ["0=1.1."], "Tail Whip", "Hit a racer with the Super Leaf's tail.", 5))
    return out


def line(id_, conditions, name, description, points):
    rc = "_".join([guard()] + conditions)
    return "%s rc:%s %s | %s | %d\n" % (id_, rc, name, description, points)


def parts_line(id_, name, description, points):
    """An achievement done in parts: its own line has no condition."""
    return "%s @parts %s | %s | %d\n" % (id_, name, description, points)


def part_line(parent, part, conditions, name):
    rc = "_".join([guard()] + conditions)
    return "@part %s %s rc:%s %s\n" % (parent, part, rc, name)


# The file's order, by group (the same as achievements.md): each group's comment, then its IDs. Every
# achievement is generated above and then placed here, so the order doesn't depend on how it's made.
ORDER = [
    ("Grand Prix: a gold trophy (save 0x70C + cup * 4 + class, trophy bits 4-5 = 3). 150cc wins read only the\n"
     "# cup's trophy, never the character it unlocks, so a save with characters unlocked another way doesn't give them",
     ["GP50_" + c for c in CUP_IDS] + ["GP100_" + c for c in CUP_IDS] + ["GP150_" + c for c in CUP_IDS]),
    ("Characters: the game's own unlock flags (save 0x4E42: bit n = Daisy, Wario, Rosalina, Metal Mario,\n"
     "# Shy Guy, Honey Queen, Wiggler, Lakitu, Mii), however they were unlocked",
     ["CHAR_DAISY", "CHAR_WARIO", "CHAR_ROSALINA", "CHAR_METAL_MARIO", "CHAR_SHY_GUY", "CHAR_HONEY_QUEEN",
      "CHAR_WIGGLER", "CHAR_LAKITU", "CHAR_MII"]),
    ("The Mirror class: unlocked, then its cups (class 3), and the title screens a gold trophy everywhere unlocks\n"
     "# (save 0x4E54 bit 0)",
     ["MIRROR_UNLOCK"] + ["GPM_" + c for c in CUP_IDS] + ["TITLE_HOLDER"]),
    ("Three stars in every cup of a class (rank bits 1-3 = 6), and every race of a cup won (live)",
     ["STARS_50", "STARS_100", "STARS_150", "STARS_MIRROR", "PERFECT_CUP"]),
    ("Coins collected in races (save 0x4C28, u32), kart parts, and coins and parts in a race (live)",
     ["COINS_1000", "COINS_5000", "COINS_10000", "COINS_20000", "ALL_PARTS", "GOLD_RIDE", "TEN_COIN_CUP"]),
    ("Time Trials: a best time faster than the course's Expert Staff Ghost (RankingSaveData, S+0x10C)",
     ["EXPERT_" + e[0] for e in EXPERT]),
    ("First person (Vehicle +0xE8) and the gold steering wheel", ["FP_RACE", "FP_CUP", "GOLD_WHEEL"]),
    ("Items: Spiny Shells; 3 racers hit with one Star / Fire Flower / Lucky Seven (each kart counts once); the\n"
     "# Super Leaf's tail",
     ["BLUE_YOURSELF", "SPINY_DODGE", "STAR_HITS", "FIRE_HITS", "LUCKY_SEVEN", "LEAF_BLOCK"]),
    ("Racing skill (live race state, player 0): Grand Prix wins at 150cc or Mirror, and mini-turbos in any race\n"
     "# (the boost timers at Vehicle +0xFA0)",
     ["UNTOUCHABLE", "NO_ITEMS", "NO_SHORTCUTS", "LAST_TO_FIRST", "NO_LAKITU", "TOKYO_DRIFT"]),
    ("Battle: the tours are done in parts, one per battle course (race type 1 Balloon Battle, 0 Coin Runners)",
     ["BALLOON_TOUR", "COIN_TOUR", "NOT_A_SCRATCH"]),
]


HEADER_LINES = 10                   # main()'s first entries: the file's header, "@version", "@title" and "@block-plugin"


def in_order(lines):
    """The generated achievements (after the header) rearranged by ORDER, each group under its comment. An
    achievement keeps all its lines (alternatives and "@part" lines) in the order they were made."""
    blocks = {}
    for entry in lines:
        for text in entry.splitlines(keepends=True):
            word = text.split(" ", 1)[0]
            if text.startswith("#") or not text.strip():
                continue                                    # the generator's own group comments
            if word == "@part":
                blocks[text.split(" ", 2)[1]].append(text)
            else:
                blocks.setdefault(word, []).append(text)
    placed = [i for _, ids in ORDER for i in ids]
    assert sorted(placed) == sorted(blocks) and len(set(placed)) == len(placed), \
        (set(blocks) - set(placed), set(placed) - set(blocks))
    out = []
    for comment, ids in ORDER:
        out.append("\n# " + comment + "\n")
        for i in ids:
            out += blocks[i]
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "achievements", "games",
                                                             "0004000000030800", "definitions.txt")
    lines = ["# Mario Kart 7 (USA, version 1.2): achievements from the save in RAM. Generated by\n",
             "# tools/mk7_defs.py; don't edit by hand.\n",
             "\n# Version 1.2 only: a read-only word that only 1.2 has. Any other version (1.0, a cartridge never\n",
             "# updated) gets \"achievements off: needs version 1.2\" instead (needs a tracker that reads \"@version\").\n",
             "@version 1.2 0x%08X 4 0x%08X\n" % (VERSION_ADDR, VERSION_12),
             "\n# The game's name in the app and on the banner, also without the game on the console (the Japanese\n",
             "# release's own title is in Japanese, which the banner's font can't show).\n",
             "@title Mario Kart 7\n",
             "\n# CTGP-7 runs Mario Kart 7 with its own save: its progress isn't the game's.\n",
             "@block-plugin CTGP-7\n"]

    gp50 = ["Training Wheels", "Stop and Smell the Flowers", "Wish Upon a Star", "Something Special",
            "Out of Your Shell", "Going Bananas", "Turning Over a New Leaf", "Lightning Round"]
    gp100 = ["Mushrooming Talent", "Late Bloomer", "Rising Star", "Special Delivery", "Shell Shocked",
             "Top Banana", "Leaf Blower", "Greased Lightning"]
    gp150 = ["Fun Guy", "Full Bloom", "Superstar", "Extra Special", "Hard Shell", "Peel Out", "Raking It In",
             "Lightning Strikes"]
    gpm = ["Wrong Way Round", "Pressed Flowers", "Seeing Stars", "Special Effects", "Shell Game",
           "Banana Split", "Fall Back", "Strikes Twice"]
    chars = [("DAISY", "Daisy", "Hi, I'm Daisy!"), ("WARIO", "Wario", "Wario Time"),
             ("ROSALINA", "Rosalina", "Stargazer"), ("METAL_MARIO", "Metal Mario", "Heavy Metal"),
             ("SHY_GUY", "Shy Guy", "Don't Be Shy"), ("HONEY_QUEEN", "Honey Queen", "Queen Bee"),
             ("WIGGLER", "Wiggler", "Wiggle Room"), ("LAKITU", "Lakitu", "Off the Hook")]

    for cup in range(8):
        lines.append(line("GP50_" + CUP_IDS[cup], gold(cup, 0), gp50[cup], "Win the 50cc %s Cup." % CUPS[cup], 3))
    for cup in range(8):
        lines.append(line("GP100_" + CUP_IDS[cup], gold(cup, 1), gp100[cup], "Win the 100cc %s Cup." % CUPS[cup], 5))
    # 150cc wins are their own achievements (user, 2026-11-18): they read the cup's trophy, never the
    # character it unlocks, so a save with characters unlocked another way doesn't give them
    for cup in range(8):
        lines.append(line("GP150_" + CUP_IDS[cup], gold(cup, 2), gp150[cup], "Win the 150cc %s Cup." % CUPS[cup], 10))

    for n, (cid, who, name) in enumerate(chars):
        lines.append(line("CHAR_" + cid, [bit(CHARACTERS, n)], name, "Unlock %s as a racer." % who, 5))
    lines.append(line("CHAR_MII", [bit(CHARACTERS, 8)], "It's All About Mii", "Unlock your Mii as a racer.", 10))
    lines.append(line("MIRROR_UNLOCK", [bit(SEQUENCE, 4)], "Through the Looking Glass", "Unlock the Mirror class.", 10))

    for cup in range(8):
        lines.append(line("GPM_" + CUP_IDS[cup], gold(cup, 3), gpm[cup], "Win the Mirror %s Cup." % CUPS[cup], 10))
    # The alternate title screens: the game sets SEQUENCE bit 0 when it plays the true ending, after a Grand Prix
    # once every cup of 50cc, 100cc, 150cc and Mirror has a gold trophy (code 0x47073C..0x470808 sets the ending
    # type to 2, 0x480CE0 then sets the bit); from then on each start picks one of the six title screens at random
    # (0x4DE010..0x4DE040: title_capture_top, wing, screw, peach, DK, yoshi), before it always the first.
    lines.append(line("TITLE_HOLDER", [bit(SEQUENCE, 0)], "Title Holder",
                      "Unlock the alternate title screens: win a gold trophy in every cup of every class, Mirror included.",
                      25))

    for cls, (cid, cname, name, points) in enumerate([("50", "50cc", "Polished Performance", 10),
                                                      ("100", "100cc", "Star Student", 10),
                                                      ("150", "150cc", "Constellation", 25),
                                                      ("MIRROR", "Mirror", "Three-Star Reflection", 25)]):
        conditions = [c for cup in range(8) for c in three_stars(cup, cls)]
        lines.append(line("STARS_" + cid, conditions, name, "Earn a three-star ranking in every %s cup." % cname, points))

    for amount, name, points in [(1000, "Spare Change", 5), (5000, "Beast Mode", 10), (10000, "Golden Wings", 10),
                                 (20000, "Solid Gold", 25)]:
        lines.append(line("COINS_%d" % amount, ["%s>=%d" % (field("X", COINS), amount)], name,
                          "Collect %s coins in races." % format(amount, ","), points))
    parts = ["%s=h3fff" % field("", BODIES), "%s=h7f" % field("H", TIRES), "%s=h3f" % field("H", GLIDERS)]
    lines.append(line("ALL_PARTS", parts, "Fully Loaded", "Unlock every kart body, tire and glider.", 25))

    for i, (cid, course, ms) in enumerate(EXPERT):
        lines.append(line("EXPERT_" + cid, faster_than(i, ms), "%s: Expert" % course,
                          "Beat the Expert Staff Ghost's time on %s in Time Trials (%s)." % (course, clock(ms)), 5))
    lines += live_lines()
    lines = lines[:HEADER_LINES] + in_order(lines[HEADER_LINES:])

    text = "".join(lines)
    with open(out, "w", newline="\n") as f:
        f.write(text)
    count = sum(1 for l in lines if l and not l.startswith("#") and not l.startswith("\n"))
    print("%s: %d lines, %d bytes, longest condition %d chars" % (out, count, len(text),
          max(len(t) - 3 for l in lines for t in l.split(" ") if t.startswith("rc:"))))


if __name__ == "__main__":
    main()
