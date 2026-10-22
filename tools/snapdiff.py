#!/usr/bin/env python3
"""Find achievement addresses by comparing memory snapshots.

Each snapshot is an index snap<N>.txt plus one snap<N>_<ADDR>.bin per memory
region (the format the old plugin's debug build wrote; any RAM dump saved this
way works, e.g. from Azahar's GDB stub).

A candidate byte has the same value in every --before snapshot, a different
value that is the same in every --after snapshot. Use several snapshots on each
side, taken in different game situations, to filter out noise.

    # 0,1 before beating the level; 2,3 after; 4 after rebooting the game
    python tools/snapdiff.py <dumps dir> --before 0 1 --after 2 3 --check 4

    # Show the memory around an address in every snapshot
    python tools/snapdiff.py <dumps dir> --before 0 1 --after 2 3 --around 0x0812ABCD
"""
import argparse
import os
import sys

BLOCK = 4096


def load_index(folder, n):
    regions = {}
    with open(os.path.join(folder, f"snap{n}.txt")) as f:
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", help="folder with snap<N>.txt and snap<N>_*.bin")
    ap.add_argument("--before", type=int, nargs="+", required=True, help="snapshots before the event")
    ap.add_argument("--after", type=int, nargs="+", required=True, help="snapshots after the event")
    ap.add_argument("--check", type=int, nargs="*", default=[],
                    help="extra snapshots whose values are shown but not used for filtering (e.g. after a reboot)")
    ap.add_argument("--require-check", action="store_true",
                    help="also drop candidates whose --check values differ from the after value")
    ap.add_argument("--max", type=int, default=60, help="maximum candidates to print")
    ap.add_argument("--around", type=lambda s: int(s, 0), help="print memory around this address and exit")
    args = ap.parse_args()

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
