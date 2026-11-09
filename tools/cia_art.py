"""Makes the HOME Menu art for the app's CIA from the logo, on a black background.

    python tools/cia_art.py

Writes app/cia/banner.png (256x128), app/cia/icon.png (48x48) and app/cia/banner.wav (the start
of assets/hammer_on_lick.wav, in stereo and under 3 seconds, which a HOME Menu banner needs).
Run from the repo root. Needs Pillow.
"""
import array
import os
import wave

from PIL import Image

LOGO = "assets/3ds-achievements-logo.png"
SOUND = "assets/hammer_on_lick.wav"
OUT = os.path.join("app", "cia")
BANNER_MARGIN = 14
ICON_MARGIN = 2
SOUND_SECONDS = 2.9
SOUND_FADE_SECONDS = 0.6


def on_black(logo, width, height, margin):
    """The logo, as large as fits inside the margin, centered on black."""
    scale = min((width - 2 * margin) / logo.width, (height - 2 * margin) / logo.height)
    size = (max(1, round(logo.width * scale)), max(1, round(logo.height * scale)))
    resized = logo.resize(size, Image.LANCZOS)
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 255))
    canvas.alpha_composite(resized, ((width - size[0]) // 2, (height - size[1]) // 2))
    return canvas.convert("RGB")


def banner_sound(source, path):
    """The start of a mono 16-bit recording, faded out, with the same audio in both channels."""
    with wave.open(source, "rb") as f:
        if f.getnchannels() != 1 or f.getsampwidth() != 2:
            raise SystemExit(f"{source} must be 16-bit mono")
        rate = f.getframerate()
        samples = array.array("h", f.readframes(int(SOUND_SECONDS * rate)))

    fade = int(SOUND_FADE_SECONDS * rate)
    start = len(samples) - fade
    stereo = array.array("h")
    for i, sample in enumerate(samples):
        if i >= start:
            sample = int(sample * (len(samples) - i) / fade)
        stereo.append(sample)
        stereo.append(sample)

    with wave.open(path, "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(stereo.tobytes())


def main():
    logo = Image.open(LOGO).convert("RGBA")
    logo = logo.crop(logo.getchannel("A").getbbox())
    os.makedirs(OUT, exist_ok=True)
    on_black(logo, 256, 128, BANNER_MARGIN).save(os.path.join(OUT, "banner.png"))
    on_black(logo, 48, 48, ICON_MARGIN).save(os.path.join(OUT, "icon.png"))
    banner_sound(SOUND, os.path.join(OUT, "banner.wav"))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
