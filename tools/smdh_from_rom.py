"""Extracts a game's SMDH (title + icon) from a decrypted 3DS ROM for the viewer app.

    python tools/smdh_from_rom.py <game.cci|.3ds|.cxi> [out_dir]

Writes <out_dir>/<TitleID>/title.smdh (default: the current folder). Put that
folder in the SD card's /achievements/; the viewer uses it when the game isn't installed
or its card isn't inserted (the tracker also saves it whenever the game runs).
Works on decrypted ROMs only (what Azahar runs).
"""
import os
import struct
import sys

MEDIA_UNIT = 0x200


def ncch_offset(f):
    """Offset of the main NCCH: partition 0 of a .cci/.3ds, or 0 for a .cxi."""
    f.seek(0x100)
    magic = f.read(4)
    if magic == b"NCSD":
        f.seek(0x120)
        offset, _ = struct.unpack("<II", f.read(8))
        return offset * MEDIA_UNIT
    if magic == b"NCCH":
        return 0
    sys.exit("not a .cci/.3ds/.cxi file (no NCSD or NCCH header)")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "."
    with open(sys.argv[1], "rb") as f:
        base = ncch_offset(f)
        f.seek(base + 0x100)
        if f.read(4) != b"NCCH":
            sys.exit("partition 0 isn't an NCCH")
        f.seek(base + 0x118)
        program_id = struct.unpack("<Q", f.read(8))[0]
        f.seek(base + 0x188 + 7)
        if not f.read(1)[0] & 0x04:
            sys.exit("the ROM is encrypted; decrypt it first (Azahar needs decrypted ROMs too)")
        f.seek(base + 0x1A0)
        exefs_offset, _ = struct.unpack("<II", f.read(8))
        exefs = base + exefs_offset * MEDIA_UNIT

        f.seek(exefs)
        header = f.read(0x200)
        for i in range(10):
            name, offset, size = struct.unpack_from("<8sII", header, i * 16)
            if name.rstrip(b"\0") == b"icon":
                f.seek(exefs + 0x200 + offset)
                smdh = f.read(size)
                break
        else:
            sys.exit("no icon in the ExeFS")

    if smdh[:4] != b"SMDH":
        sys.exit("the icon file isn't an SMDH")
    title = smdh[8 + 0x200:8 + 0x200 + 0x80].decode("utf-16-le").split("\0")[0]  # English short title
    path = os.path.join(out_dir, "%016X" % program_id, "title.smdh")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as out:
        out.write(smdh)
    print("%s: %s (%d bytes)" % (path, title, len(smdh)))


main()
