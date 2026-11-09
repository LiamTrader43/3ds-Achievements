"""Makes the HOME Menu art for the app's CIA from the logo, on a black background.

    python tools/cia_art.py

Writes app/cia/banner.png (256x128), app/cia/icon.png (48x48) and app/cia/banner.wav (a moment
of silence; a banner has to have sound). Run from the repo root. Needs Pillow.
"""
import os
import struct

from PIL import Image

LOGO = "assets/3ds-achievements-logo.png"
OUT = os.path.join("app", "cia")
BANNER_MARGIN = 14
ICON_MARGIN = 2


def on_black(logo, width, height, margin):
    """The logo, as large as fits inside the margin, centered on black."""
    scale = min((width - 2 * margin) / logo.width, (height - 2 * margin) / logo.height)
    size = (max(1, round(logo.width * scale)), max(1, round(logo.height * scale)))
    resized = logo.resize(size, Image.LANCZOS)
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 255))
    canvas.alpha_composite(resized, ((width - size[0]) // 2, (height - size[1]) // 2))
    return canvas.convert("RGB")


def silence(path, seconds=1, rate=32000):
    frames = rate * seconds
    data = b"\0\0" * frames
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) + b"data" + struct.pack("<I", len(data)) + data)


def main():
    logo = Image.open(LOGO).convert("RGBA")
    logo = logo.crop(logo.getchannel("A").getbbox())
    os.makedirs(OUT, exist_ok=True)
    on_black(logo, 256, 128, BANNER_MARGIN).save(os.path.join(OUT, "banner.png"))
    on_black(logo, 48, 48, ICON_MARGIN).save(os.path.join(OUT, "icon.png"))
    silence(os.path.join(OUT, "banner.wav"))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
