#!/usr/bin/env python3
"""Live course records for Super Mario 3D Land (USA, 0004000000054000).

While the game runs, each save file has a list of 0x20-byte records, one per
map spot (courses, Mystery Boxes, Toad Houses, castles, airships), in map
order for Worlds 1-8 and Special Worlds 1-8:

    +0x00  pointer to the stage's internal name ("FirstStage" = 1-1)
    +0x04  status, u32: 0 locked, 1 not beaten, 2 beaten
    +0x08  pointer to the spot's flags (u32; same as the save record's byte 1):
           0x10 goal reached (set at the pole), 0x08 shown on the map,
           0x01 a star medal (bits 0-2 are probably the three medals)
    +0x0C  u32 x4, same as save record bytes 5-8 (meaning unknown)
    +0x10  flagpole height, 0-100 (save byte 9; 100 seen on two courses,
           probably the top)
    +0x1C  clear time in seconds (save byte 10; 72 matched the timer)

Each world is a 0x100-byte block: a header record (no name, status 2) and 7
spots. World 8 takes two blocks (its map has several parts, each with a
header). Special World 8's first slot is ChampionshipSpStage instead of a
header; it reads 2 even on a new file, so its status means something else.
The status changes the moment a course is beaten, unlike the save copy at
0x15910850 (which only changes when the game saves). The lists sit at the same
addresses every boot: file A's starts at 0x1618FD78, file B's 0x2990 bytes
later, file C's 0x2990 after that.

    python tools/sm3dl_live.py heap.bin          # Markdown table
    python tools/sm3dl_live.py heap.bin --status # plus each spot's status
                                                 # and flags

heap.bin is a dump of 0x14000000..0x17CAE000 (Azahar's GDB stub:
dump binary memory heap.bin 0x14000000 0x17CAE000); the names are read from it.
"""
import sys

HEAP = 0x14000000
LIST_A = 0x1618FD78     # file A's first record (World 1's header)
FILE_STRIDE = 0x2990
RECORD = 0x20
BLOCK = 0x100
# (world, number of 0x100-byte blocks), in list order.
WORLDS = [(f"World {n}", 1) for n in range(1, 8)] + [("World 8", 2)] + \
         [(f"Special World {n}", 1) for n in range(1, 9)]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        sys.exit(__doc__)
    heap = open(args[0], "rb").read()
    show_status = "--status" in sys.argv

    def u32(address):
        o = address - HEAP
        return int.from_bytes(heap[o:o + 4], "little")

    def name(pointer):
        o = pointer - HEAP
        if not 0 <= o < len(heap):
            return None
        text = heap[o:o + 64].split(b"\0")[0]
        return text.decode("ascii") if text and all(32 <= c < 127 for c in text) else None

    block = LIST_A
    for world, blocks in WORLDS:
        print(f"\n### {world}\n")
        print("| Spot | Stage | Status A | Status B | Status C | Flags A | Flags B | Flags C |"
              + (" A | B | C |" if show_status else ""))
        print("|---|---|---|---|---|---|---|---|" + ("---|---|---|" if show_status else ""))
        spot = 0
        for address in range(block, block + blocks * BLOCK, RECORD):
            pointer = u32(address)
            if pointer == 0:                    # header
                continue
            stage = name(pointer) or "?"
            spot += 1
            status = [address + 4 + f * FILE_STRIDE for f in range(3)]
            flags = [u32(address + 8 + f * FILE_STRIDE) for f in range(3)]
            row = f"| {spot} | {stage} | " + " | ".join(f"`0x{s:08X}`" for s in status + flags) + " |"
            if show_status:
                row += " " + " | ".join(f"{u32(s)}/0x{u32(fl):02X}" for s, fl in zip(status, flags)) + " |"
            print(row)
        block += blocks * BLOCK


if __name__ == "__main__":
    main()
