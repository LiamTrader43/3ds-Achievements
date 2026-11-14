"""Converts an image (PNG, JPG, ...) into a game's background for the Achievements app.

    python tools/hero_art.py <image> <TitleID> [game folder]

Writes hero.bin in the game's folder (default: achievements/<TitleID>): the image cropped
from the center to fill the 400x240 top screen plus 16 more columns on each side, 432x240,
as raw BGR8 in the 3DS framebuffer's order (column by column from the left, each column
bottom row first). The extra columns come into view at the edge when the 3D slider is up
(the background sits behind the screen, so one eye sees it moved sideways). Use an image at
least 1.8 times as wide as it is tall, or it's cropped top and bottom to that. The file goes
in /achievements/<TitleID>/ on the SD card. Needs Pillow.
"""
import os
import re
import sys

from PIL import Image

MARGIN = 16                         # app/source/hero.h: HERO_MARGIN
WIDTH, HEIGHT = 400 + 2 * MARGIN, 240


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    source, title_id = sys.argv[1], sys.argv[2].upper()
    if not re.fullmatch(r"[0-9A-F]{16}", title_id):
        sys.exit("The title ID must be 16 hex digits, like 0004000000033500.")
    directory = sys.argv[3] if len(sys.argv) > 3 else os.path.join("achievements", title_id)

    image = Image.open(source).convert("RGB")
    scale = max(WIDTH / image.width, HEIGHT / image.height)
    crop_w, crop_h = WIDTH / scale, HEIGHT / scale
    left, top = (image.width - crop_w) / 2, (image.height - crop_h) / 2
    image = image.resize((WIDTH, HEIGHT), Image.LANCZOS, box=(left, top, left + crop_w, top + crop_h))

    pixels = image.load()
    out = bytearray()
    for x in range(WIDTH):
        for y in range(HEIGHT - 1, -1, -1):
            r, g, b = pixels[x, y]
            out += bytes((b, g, r))

    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "hero.bin")
    with open(path, "wb") as f:
        f.write(out)
    print(f"{path}: {len(out)} bytes")


if __name__ == "__main__":
    main()
