"""Writes versions.txt for the release zip, so a fresh install isn't offered the files it just got.

    python tools/make_versions.py

Lists every definitions.txt, hero.bin and trophy.bin in sysmodule/dist/achievements with its Git
blob ID, the same IDs the app compares against the main branch. Run from the repo root, after the
files in dist are final and before zipping.
"""
import hashlib
import os
import re

DIST = os.path.join("sysmodule", "dist", "achievements")
FILES = ("definitions.txt", "hero.bin", "trophy.bin")


def blob_id(path):
    with open(path, "rb") as f:
        data = f.read()
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def main():
    lines = []
    for game in sorted(os.listdir(DIST)):
        if not re.fullmatch(r"[0-9A-Fa-f]{16}", game):
            continue
        for name in FILES:
            path = os.path.join(DIST, game, name)
            if os.path.isfile(path):
                lines.append(f"{game}/{name} {blob_id(path)}\n")
    with open(os.path.join(DIST, "versions.txt"), "w", newline="\n") as f:
        f.writelines(lines)
    print("".join(lines), end="")


if __name__ == "__main__":
    main()
