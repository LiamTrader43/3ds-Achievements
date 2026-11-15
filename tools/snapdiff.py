#!/usr/bin/env python3
"""Find achievement addresses by comparing memory snapshots.

Each snapshot is one snap<N>_<ADDR>.bin per memory region, where ADDR is the
3DS address the dump starts at in hex, e.g. from Azahar's GDB stub:

    dump binary memory snap0_14000000.bin 0x14000000 0x18000000

An index snap<N>.txt ("<start> <size> <file>" per line, hex), as the old
plugin's debug build wrote, is used instead when it exists.

A candidate byte has the same value in every --before snapshot, a different
value that is the same in every --after snapshot. Use several snapshots on each
side, taken in different game situations, to filter out noise.

    # 0,1 before beating the level; 2,3 after; 4 after rebooting the game
    python tools/snapdiff.py <dumps dir> --before 0 1 --after 2 3 --check 4

    # Show the memory around an address in every snapshot
    python tools/snapdiff.py <dumps dir> --before 0 1 --after 2 3 --around 0x0812ABCD

    # Find a pointer to something that moves: it was at 0x0871E944 in snapshot 0
    # and at 0x0874A6F4 in snapshot 5 (another run)
    python tools/snapdiff.py <dumps dir> --pointers-to 0:0x0871E944 5:0x0874A6F4
"""
import argparse
import array
import os
import sys

BLOCK = 4096


def load_index(folder, n):
    regions = {}
    index = os.path.join(folder, f"snap{n}.txt")
    if not os.path.exists(index):
        # No index: every snap<N>_<hex start address>.bin is a region (e.g. gdb's
        # "dump binary memory snap0_14000000.bin 0x14000000 0x18000000").
        prefix = f"snap{n}_"
        for name in os.listdir(folder):
            if name.startswith(prefix) and name.endswith(".bin"):
                try:
                    base = int(name[len(prefix):-4], 16)
                except ValueError:
                    continue
                regions[base] = (os.path.getsize(os.path.join(folder, name)), name)
        if not regions:
            sys.exit(f"no snap{n}.txt or snap{n}_<address>.bin in {folder}")
        return regions
    with open(index) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 3:
                regions[int(parts[0], 16)] = (int(parts[1], 16), parts[2])
    return regions


def read_file(folder, name):
    with open(os.path.join(folder, name), "rb") as f:
        return f.read()


def find_candidates(before, after, check, require_check):
    """Yields (offset, before value, after value, check values) for one region."""
    b0, a0 = before[0], after[0]
    for start in range(0, len(b0), BLOCK):
        end = min(start + BLOCK, len(b0))
        if b0[start:end] == a0[start:end]:
            continue
        for i in range(start, end):
            v, w = b0[i], a0[i]
            if v == w:
                continue
            if any(s[i] != v for s in before[1:]) or any(s[i] != w for s in after[1:]):
                continue
            checks = [s[i] for s in check]
            if require_check and any(c != w for c in checks):
                continue
            yield i, v, w, checks


def print_around(folder, indexes, snaps, addr):
    start = (addr & ~0xF) - 0x20
    print(f"Memory around 0x{addr:08X}:")
    for n in snaps:
        for base, (size, name) in indexes[n].items():
            if base <= start and start + 0x50 <= base + size:
                with open(os.path.join(folder, name), "rb") as f:
                    f.seek(start - base)
                    data = f.read(0x50)
                print(f"  snap{n}:")
                for row in range(0, 0x50, 0x10):
                    cells = " ".join(
                        f"[{b:02X}]" if start + row + j == addr else f" {b:02X} "
                        for j, b in enumerate(data[row:row + 0x10]))
                    print(f"    {start + row:08X}  {cells}")
                break
        else:
            print(f"  snap{n}: address not in any dumped region")


def read_word(folder, index, addr):
    """The 4-byte little-endian value at addr in one snapshot, or None if it wasn't dumped."""
    for base, (size, name) in index.items():
        if base <= addr and addr + 4 <= base + size:
            with open(os.path.join(folder, name), "rb") as f:
                f.seek(addr - base)
                return int.from_bytes(f.read(4), "little")
    return None


def find_pointers(folder, indexes, targets, max_offset):
    """targets: (snapshot, address of the thing in that snapshot) pairs. Returns
    (location, offset) for every 4-byte aligned value that points at most max_offset
    bytes before the address, at the same location and offset in every snapshot."""
    first, addr = targets[0]
    found = []
    for base, (size, name) in sorted(indexes[first].items()):
        data = read_file(folder, name)
        words = array.array("I")
        assert words.itemsize == 4
        words.frombytes(data[:len(data) // 4 * 4])
        low = addr - max_offset
        for i, value in enumerate(words):
            if low <= value <= addr:
                found.append((base + i * 4, addr - value))
    for n, other in targets[1:]:
        found = [(loc, off) for loc, off in found if read_word(folder, indexes[n], loc) == other - off]
    return sorted(found, key=lambda f: (f[1], f[0]))


def main_pointers(args):
    targets = []
    for item in args.pointers_to:
        n, _, addr = item.partition(":")
        if not addr:
            sys.exit(f"--pointers-to takes <snapshot>:<address>, e.g. 0:0x0871E944 (got {item})")
        targets.append((int(n), int(addr, 0)))
    indexes = {n: load_index(args.dir, n) for n, _ in targets}
    found = find_pointers(args.dir, indexes, targets, args.max_offset)
    print(f"{len(found)} pointer(s) to " + ", ".join(f"0x{a:08X} (snap{n})" for n, a in targets))
    if found:
        print("location    offset   points to")
    for loc, off in found[:args.max]:
        values = " ".join(f"0x{a - off:08X}" for _, a in targets)
        print(f"0x{loc:08X}  +0x{off:<5X} {values}")
    if len(found) > args.max:
        print(f"... {len(found) - args.max} more (raise --max to see them)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", help="folder with snap<N>.txt and snap<N>_*.bin")
    ap.add_argument("--before", type=int, nargs="+", default=[], help="snapshots before the event")
    ap.add_argument("--after", type=int, nargs="+", default=[], help="snapshots after the event")
    ap.add_argument("--pointers-to", nargs="+", metavar="N:ADDR",
                    help="list pointers to ADDR in snapshot N (give several N:ADDR from different runs "
                         "to keep only the pointers that follow the thing when it moves)")
    ap.add_argument("--max-offset", type=lambda s: int(s, 0), default=0x10000,
                    help="for --pointers-to: how far before ADDR a pointer may point (default 0x10000)")
    ap.add_argument("--check", type=int, nargs="*", default=[],
                    help="extra snapshots whose values are shown but not used for filtering (e.g. after a reboot)")
    ap.add_argument("--require-check", action="store_true",
                    help="also drop candidates whose --check values differ from the after value")
    ap.add_argument("--max", type=int, default=60, help="maximum candidates to print")
    ap.add_argument("--around", type=lambda s: int(s, 0), help="print memory around this address and exit")
    args = ap.parse_args()

    if args.pointers_to:
        main_pointers(args)
        return
    if not args.before or not args.after:
        ap.error("--before and --after are needed (or --pointers-to)")

    snaps = args.before + args.after + args.check
    indexes = {n: load_index(args.dir, n) for n in snaps}

    if args.around is not None:
        print_around(args.dir, indexes, snaps, args.around)
        return

    bases = set.intersection(*(set(ix) for ix in indexes.values()))
    for n in snaps:
        for base in sorted(set(indexes[n]) - bases):
            print(f"note: region 0x{base:08X} is only in some snapshots; skipped", file=sys.stderr)

    found = []
    for base in sorted(bases):
        if len({indexes[n][base][0] for n in snaps}) != 1:
            print(f"note: region 0x{base:08X} changed size between snapshots; skipped", file=sys.stderr)
            continue
        data = {n: read_file(args.dir, indexes[n][base][1]) for n in snaps}
        for off, v, w, checks in find_candidates([data[n] for n in args.before], [data[n] for n in args.after],
                                                 [data[n] for n in args.check], args.require_check):
            found.append((base + off, v, w, checks))

    header = "address     before  after"
    if args.check:
        header += "  check(" + ",".join(map(str, args.check)) + ")"
    print(f"{len(found)} candidate byte(s)")
    if found:
        print(header)
    for addr, v, w, checks in found[:args.max]:
        line = f"0x{addr:08X}  {v:02X}      {w:02X}"
        if checks:
            line += "     " + " ".join(f"{c:02X}" for c in checks)
            if any(c != w for c in checks):
                line += "  (differs)"
        print(line)
    if len(found) > args.max:
        print(f"... {len(found) - args.max} more (raise --max to see them)")


if __name__ == "__main__":
    main()
