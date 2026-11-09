"""Converts images into a game's achievement icons for the Achievements app.

    python tools/trophy_art.py <TitleID> <unlocked image> [locked image] [--out game folder]

Writes trophy.bin in the game's folder (default: achievements/<TitleID>): the unlocked icon,
then the locked one, each 48x48 RGB565 in the SMDH tile order the app draws. Without a locked
image, the unlocked one is used in dimmed greyscale. Transparent areas become black, the app's
background. The file goes in /achievements/<TitleID>/ on the SD card; a game without one gets
the normal trophy. Needs Pillow.
"""
import os
import re
import struct
import sys

from PIL import Image, ImageOps

SIZE = 48


def load(path):
    image = Image.open(path).convert("RGBA")
    backdrop = Image.new("RGBA", image.size, (0, 0, 0, 255))
    return Image.alpha_composite(backdrop, image).convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)


def tiled565(image):
    pixels = image.load()
    out = bytearray()
    for i in range(SIZE * SIZE):
        tile, inner = divmod(i, 64)
        x = (tile % 6) * 8 + ((inner & 1) | ((inner >> 1) & 2) | ((inner >> 2) & 4))
        y = (tile // 6) * 8 + (((inner >> 1) & 1) | ((inner >> 2) & 2) | ((inner >> 3) & 4))
        r, g, b = pixels[x, y]
        out += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return out


def main():
    args = sys.argv[1:]
    directory = None
    if "--out" in args:
        at = args.index("--out")
        directory = args[at + 1]
        del args[at:at + 2]
    if len(args) not in (2, 3):
        sys.exit(__doc__)
    title_id = args[0].upper()
    directory = directory or os.path.join("achievements", title_id)
    if not re.fullmatch(r"[0-9A-F]{16}", title_id):
        sys.exit("The title ID must be 16 hex digits, like 0004000000033500.")

    unlocked = load(args[1])
    if len(args) == 3:
        locked = load(args[2])
    else:
        locked = ImageOps.grayscale(unlocked).point(lambda v: v * 45 // 100).convert("RGB")

    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "trophy.bin")
    with open(path, "wb") as f:
        f.write(tiled565(unlocked) + tiled565(locked))
    print(f"{path}: {os.path.getsize(path)} bytes")


if __name__ == "__main__":
    main()
