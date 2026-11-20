"""Makes a game's icons.bin: a picture of its own for each achievement (optional).

    python tools/achievement_icons.py <TitleID> <pictures folder> [--out game folder]

The folder holds a picture per achievement, named after its ID: <ID>.png (or .jpg, .gif, .bmp,
.webp), and if you like <ID>.locked.png for how it looks while locked; without one, the app and
the tracker show the unlocked picture greyed and darker. Achievements without a picture keep the
game's trophy.bin, or the usual trophy. Pictures are made 48x48 (a picture that isn't square is
cut to its middle square); parts that are mostly transparent are see-through.

Writes icons.bin in the game's folder (default: achievements/<TitleID>), which goes in
/achievements/<TitleID>/ on the SD card. If the folder has a definitions.txt, the pictures are
checked against its IDs. The format is described in sysmodule/tracker/source/icons.h. Needs
Pillow.
"""
import os
import re
import struct
import sys

from PIL import Image, ImageOps

SIZE = 48
PICTURE_BYTES = SIZE * SIZE * 2
HEADER_BYTES = 8
ENTRY_BYTES = 40
MOST = 256                          # entries (icons.h ICONS_MOST)
MAGIC = b"A3IC"
FORMAT = 1
DARKEST = 0x0821                    # 0 is see-through, so an opaque black pixel is this
EXTENSIONS = (".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp")
LOCKED = ".locked"


def load(path):
    """The picture as 48x48 RGBA (straight alpha)."""
    image = Image.open(path).convert("RGBA").convert("RGBa")     # premultiplied while it's scaled
    image = ImageOps.fit(image, (SIZE, SIZE), Image.LANCZOS)
    return image.convert("RGBA")


def tiled565(image):
    pixels = image.load()
    out = bytearray()
    for i in range(SIZE * SIZE):
        tile, inner = divmod(i, 64)
        x = (tile % 6) * 8 + ((inner & 1) | ((inner >> 1) & 2) | ((inner >> 2) & 4))
        y = (tile // 6) * 8 + (((inner >> 1) & 1) | ((inner >> 2) & 2) | ((inner >> 3) & 4))
        r, g, b, a = pixels[x, y]
        if a < 128:
            value = 0
        else:
            value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            value = value or DARKEST
        out += struct.pack("<H", value)
    return bytes(out)


def definition_ids(path):
    """The achievement IDs in a definitions.txt, in its order (None if there isn't one)."""
    if not os.path.isfile(path):
        return None
    ids = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            words = line.split()
            if not words or words[0].startswith("#") or words[0].startswith("@") or len(words) < 2:
                continue
            if words[0] not in ids and len(words[0]) < 32:
                ids.append(words[0])
    return ids


def pictures(folder):
    """{ID: (unlocked path, locked path or None)} from the folder's file names."""
    found, locked = {}, {}
    for name in sorted(os.listdir(folder)):
        stem, extension = os.path.splitext(name)
        if extension.lower() not in EXTENSIONS:
            continue
        path = os.path.join(folder, name)
        if stem.lower().endswith(LOCKED):
            target = locked
            stem = stem[:-len(LOCKED)]
        else:
            target = found
        if stem in target:
            sys.exit(f"Two pictures for {stem}{' (locked)' if target is locked else ''}: {target[stem]} and {path}")
        target[stem] = path
    for stem, path in locked.items():
        if stem not in found:
            sys.exit(f"{path} is a locked picture, but there's no {stem}.png for while it's unlocked.")
    return {stem: (path, locked.get(stem)) for stem, path in found.items()}


def build(entries):
    """icons.bin's bytes for [(ID, unlocked bytes, locked bytes or None)]."""
    index = bytearray(MAGIC + struct.pack("<HH", FORMAT, len(entries)))
    body = bytearray()
    offset = HEADER_BYTES + ENTRY_BYTES * len(entries)
    for id_, unlocked, locked in entries:
        where = offset + len(body)
        body += unlocked
        locked_at = 0
        if locked is not None:
            locked_at = offset + len(body)
            body += locked
        index += id_.encode("ascii").ljust(32, b"\0") + struct.pack("<II", where, locked_at)
    return bytes(index + body)


def main():
    args = sys.argv[1:]
    directory = None
    if "--out" in args:
        at = args.index("--out")
        directory = args[at + 1]
        del args[at:at + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    title_id, folder = args[0].upper(), args[1]
    if not re.fullmatch(r"[0-9A-F]{16}", title_id):
        sys.exit("The title ID must be 16 hex digits, like 0004000000033500.")
    if not os.path.isdir(folder):
        sys.exit(f"{folder} isn't a folder.")
    directory = directory or os.path.join("achievements", title_id)

    found = pictures(folder)
    for id_ in found:
        if not (0 < len(id_) < 32) or not id_.isascii() or any(c.isspace() for c in id_):
            sys.exit(f"{id_} can't be an achievement ID (1-31 characters, no spaces).")
    if not found:
        sys.exit(f"No pictures in {folder} (<ID>.png, <ID>.locked.png).")
    if len(found) > MOST:
        sys.exit(f"{len(found)} pictures: an icons.bin holds at most {MOST}.")

    ids = definition_ids(os.path.join(directory, "definitions.txt"))
    order = list(found)
    if ids is not None:
        unknown = [id_ for id_ in found if id_ not in ids]
        if unknown:
            sys.exit("No achievement has these IDs in definitions.txt: " + ", ".join(unknown))
        order = [id_ for id_ in ids if id_ in found]        # the definitions' order
        missing = len(ids) - len(order)
        if missing:
            print(f"{missing} of {len(ids)} achievements have no picture (they keep the game's trophy).")

    entries = []
    for id_ in order:
        unlocked, locked = found[id_]
        entries.append((id_, tiled565(load(unlocked)), tiled565(load(locked)) if locked else None))
    data = build(entries)

    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "icons.bin")
    with open(path, "wb") as f:
        f.write(data)
    with_locked = sum(1 for e in entries if e[2] is not None)
    count = f"{len(entries)} picture{'' if len(entries) == 1 else 's'}"
    print(f"{path}: {count} ({with_locked} with a locked picture of its own), {len(data)} bytes")


if __name__ == "__main__":
    main()
