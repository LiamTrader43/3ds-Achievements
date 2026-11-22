"""Writes versions.txt for the release zip, so a fresh install isn't offered the files it just got.

    python tools/make_versions.py

Lists every definitions.txt, hero.bin, trophy.bin, icons.bin and version.txt in
sysmodule/dist/achievements/games with its Git blob ID (as "<TitleID>/<file>", the keys the app uses), the same IDs the app compares against the main
branch. Run from the repo root, after the files in dist are final and before zipping.
"""
import hashlib
import os
import re

DIST = os.path.join("sysmodule", "dist", "achievements")
FILES = ("definitions.txt", "hero.bin", "trophy.bin", "icons.bin", "version.txt")


def blob_id(path):
    with open(path, "rb") as f:
        data = f.read()
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def main():
    lines = []
    games = os.path.join(DIST, "games")
    for game in sorted(os.listdir(games)):
        if not re.fullmatch(r"[0-9A-Fa-f]{16}", game):
            continue
        for name in FILES:
            path = os.path.join(games, game, name)
            if os.path.isfile(path):
                lines.append(f"{game}/{name} {blob_id(path)}\n")
    with open(os.path.join(DIST, "versions.txt"), "w", newline="\n") as f:
        f.writelines(lines)
    print("".join(lines), end="")


if __name__ == "__main__":
    main()
