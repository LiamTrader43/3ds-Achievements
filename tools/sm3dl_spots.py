#!/usr/bin/env python3
"""Map-spot addresses for Super Mario 3D Land (USA, 0004000000054000).

Every spot on the world maps (courses, Mystery Boxes, Toad Houses, castles)
has a record in the save whose first byte is its status: 00 locked behind star
medals, 01 not beaten, 02 beaten. The game keeps the whole save in memory at
0x15910850, so a record's address is 0x15910850 + offset (+0x983 per file).

    python tools/sm3dl_spots.py                 # address table (Markdown)
    python tools/sm3dl_spots.py GameData.bin    # plus each spot's status in files A/B/C

Layout found by beating spots one at a time and diffing the save; see the
Super Mario 3D Land notes in CLAUDE.md.
"""
import sys

SAVE_RAM = 0x15910850
SLOT = 0x983

# World blocks: start offset (file A) and record offsets within the block, in
# map order. Flags: T = Toad House (19-byte record), L = status 00 on a new
# file (locked; position inferred from spacing), ? = position uncertain,
# X = already 02 on a new file (don't use for achievements).
WORLDS = [
    (0x2BF8, "0 C 18 8D 99L A5T B8"),
    (0x2CBC, "0 C 18 8D 99 A5T B8 C4 D0 DC E8 F4 100L"),
    (0x2DC8, "0 C 18 8D 99L A5 B1 BD C9 D5 E1L ED F9T 10C 118L 124"),
    # C9..E8 holds a Toad House and a locked record in an order spacing can't tell.
    (0x2EF8, "0 C 18 8D 99L A5 B1 BD C9? D5L? E8 F4 100 10C 118 124L 130 13C 148L 154 160L 16C 178 184T"),
    (0x308F, "0 C 18 8DL 99 A5 B1 BDT D0 DC E8L F4"),
    (0x318F, "0 C 18 8D 99 A5L B1 BD C9 D5T E8 F4 100L 10C"),
    (0x32A7, "0 C 18 8D 99 A5L B1 BD C9 D5T E8 F4 100L 10C"),
    # Where World 8 ends is unknown; the slot's own fields follow it.
    (0x33BF, "0 C 18 8D 99 A5L B1X BDL C9L D5L E1"),
]

# Spots identified in play: (world, spot number) -> name.
NAMES = {
    (1, 1): "1-1", (1, 2): "1-2", (1, 3): "Mystery Box", (1, 4): "1-3",
    (1, 5): "black ? box", (1, 6): "Toad House", (1, 7): "castle",
    (2, 1): "2-1", (2, 2): "2-2",
}
VERIFIED = {(1, 1), (1, 2), (1, 3), (1, 4), (1, 6), (1, 7), (2, 1), (2, 2)}

STATUS = {0x00: "locked", 0x01: "-", 0x02: "beaten"}


def spots():
    for w, (start, layout) in enumerate(WORLDS, 1):
        for n, token in enumerate(layout.split(), 1):
            flags = token.lstrip("0123456789ABCDEF")
            yield w, n, start + int(token[:len(token) - len(flags)], 16), flags


def main():
    save = open(sys.argv[1], "rb").read() if len(sys.argv) > 1 else None

    columns = ["Spot", "Name", "Save offset", "File A", "File B", "File C"]
    if save:
        columns += ["A", "B", "C"]
    columns.append("Note")
    header = "| " + " | ".join(columns) + " |"
    rule = "|" + "---|" * len(columns)

    world = None
    for w, n, off, flags in spots():
        if w != world:
            world = w
            print(f"\n### World {w}\n\n{header}\n{rule}")
        notes = []
        if (w, n) in VERIFIED:
            notes.append("verified")
        if "T" in flags:
            notes.append("Toad House (19 bytes)")
        if "L" in flags:
            notes.append("locked on a new file")
        if "?" in flags:
            notes.append("position uncertain")
        if "X" in flags:
            notes.append("already beaten on a new file; don't use")
        cells = [f"{w}.{n}", NAMES.get((w, n), ""), f"+0x{off:04X}"]
        cells += [f"`0x{SAVE_RAM + off + k * SLOT:08X}`" for k in range(3)]
        if save:
            cells += [STATUS.get(save[off + k * SLOT], f"{save[off + k * SLOT]:02X}") for k in range(3)]
        cells.append(", ".join(notes))
        print("| " + " | ".join(cells) + " |")


if __name__ == "__main__":
    main()
