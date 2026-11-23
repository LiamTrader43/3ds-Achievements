"""Builds a GitHub release's assets from sysmodule/dist:

- 3ds-achievements.zip: no wrapping folder, these at the root: 3ds/achievements-viewer.3dsx,
  cias/achievements.cia, luma/sysmodules/0004013000AC4102.cxi, achievements/, licenses/, forward
  slashes in every entry. Keep the name and layout: Universal-Updater's install scripts, the app's
  own updater and app stores depend on them.
- SHA256SUMS.txt: the zip's SHA-256 in sha256sum's format ("<hex>  3ds-achievements.zip"), for app
  stores that check downloads against it (forge.store does). Upload it with the zip.

    python tools/make_release_zip.py <output folder>

Then checks the zip against dist, and the sums file against the zip. Run tools/make_versions.py first.
"""
import hashlib
import os
import sys
import zipfile

ZIP_NAME = "3ds-achievements.zip"
SUMS_NAME = "SHA256SUMS.txt"
TOP = ["3ds", "achievements", "cias", "licenses", "luma"]
REQUIRED = ["3ds/achievements-viewer.3dsx", "cias/achievements.cia", "luma/sysmodules/0004013000AC4102.cxi",
            "achievements/versions.txt"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    dist = os.path.join(repo, "sysmodule", "dist")
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, ZIP_NAME)
    sums = os.path.join(out_dir, SUMS_NAME)

    assert sorted(os.listdir(dist)) == TOP, sorted(os.listdir(dist))
    files = []
    for top in TOP:
        for root, dirs, names in os.walk(os.path.join(dist, top)):
            dirs.sort()
            for name in sorted(names):
                path = os.path.join(root, name)
                arc = os.path.relpath(path, dist).replace(os.sep, "/")
                assert name.lower() not in ("thumbs.db", "desktop.ini", ".ds_store") and "progress.txt" not in arc, arc
                files.append((path, arc))
    for r in REQUIRED:
        assert any(arc == r for _, arc in files), "missing " + r

    if os.path.exists(out):
        os.remove(out)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        written = set()
        for path, arc in files:
            parts = arc.split("/")
            for i in range(1, len(parts)):              # folder entries, as most zip tools write them
                d = "/".join(parts[:i]) + "/"
                if d not in written:
                    z.writestr(zipfile.ZipInfo(d, date_time=(2026, 11, 12, 0, 0, 0)), "")
                    written.add(d)
            z.write(path, arc)

    # Check: every entry readable (CRC), forward slashes only, contents equal to dist.
    with zipfile.ZipFile(out) as z:
        assert z.testzip() is None
        names = z.namelist()
        assert all("\\" not in n for n in names)
        assert all(n.split("/")[0] in TOP for n in names)
        for path, arc in files:
            with open(path, "rb") as f:
                assert z.read(arc) == f.read(), arc
        for n in names:
            print("%9d  %s" % (z.getinfo(n).file_size, n))

    digest = sha256(out)
    with open(sums, "w", encoding="ascii", newline="\n") as f:
        f.write("%s  %s\n" % (digest, ZIP_NAME))
    with open(sums, encoding="ascii") as f:
        line = f.read()
    assert line == "%s  %s\n" % (sha256(out), ZIP_NAME), line

    print("\n%s: %d files, %d bytes" % (out, len(files), os.path.getsize(out)))
    print("%s: %s" % (sums, line.strip()))
    print("Upload both to the release.")


main()
