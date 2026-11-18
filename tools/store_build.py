"""Builds the store's files (app/source/store_file.h) from its list of items.

    python3 tools/store_build.py <store_key.h> <items.txt> <themes dir> <out dir> [<copy dir>]

"make store" in app/ runs it: items.txt is app/store/items.txt, the out dir the repository's
achievements/store/ (the app downloads new and changed files from there, like games' files) and
the copy dir sysmodule/dist/achievements/store/ (for the release zip). It writes items.bin (the
list) and, for each theme item, <theme>.bin from <themes dir>/<theme>.theme, each only if it
changed (the same input always gives the same file).

items.txt, a line per item ('#' starts a comment):

    <ID> <price> <kind> <value> <name> | <description>
    THEME_DS 100 theme ds DS theme | The app in the style of the original DS menu.

ID: 1-31 of A-Z a-z 0-9 _ -, saved in each buyer's purchases.txt, so never change it once the
item is sold. price: Play Coins, 0-300. kind: theme (value: the theme's id, a themes/<id>/ add-on
in app/). name: up to 47 characters, description up to 191, plain ASCII. The app checks the same
rules and leaves out lines it can't use, so a newer kind of item doesn't show in older apps.
"""
import os
import re
import struct
import sys

sys.dont_write_bytecode = True     # (no __pycache__ in the repository)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import store_pack  # noqa: E402

LIST_NAME = "items"
ID = re.compile(r"[A-Za-z0-9_-]{1,31}")
THEME = re.compile(r"[a-z0-9_-]{1,15}")
THEME_MAGIC = 0x48543341        # "A3TH" (app/source/theme_api.h)
KINDS = ("theme",)


def read_list(path):
    """The items in items.txt, as (line number, ID, price, kind, value, name, description)."""
    items, problems, ids = [], [], set()
    # Packed with plain line ends: git may check the list out with Windows ones, and items.bin
    # mustn't change with them (every app would download it again).
    raw = open(path, "rb").read().replace(b"\r\n", b"\n")
    for number, line in enumerate(raw.split(b"\n"), 1):
        try:
            text = line.decode("ascii").strip()
        except UnicodeDecodeError:
            problems.append("line %d: only plain ASCII" % number)
            continue
        if not text or text.startswith("#"):
            continue
        if any(ord(c) < 32 and c != "\t" for c in text):
            problems.append("line %d: a control character" % number)
            continue
        parts = text.split(None, 4)
        if len(parts) < 5:
            problems.append("line %d: needs <ID> <price> <kind> <value> <name>" % number)
            continue
        item_id, price, kind, value, rest = parts
        name, _, description = rest.partition("|")
        name, description = name.strip(), description.strip()
        if not ID.fullmatch(item_id):
            problems.append("line %d: ID %r isn't 1-31 of A-Z a-z 0-9 _ -" % (number, item_id))
        elif item_id in ids:
            problems.append("line %d: ID %s is already in the list" % (number, item_id))
        if not price.isdigit() or int(price) > 300:
            problems.append("line %d: price %r isn't 0-300" % (number, price))
        if kind not in KINDS:
            problems.append("line %d: kind %r isn't one of %s" % (number, kind, ", ".join(KINDS)))
        elif kind == "theme" and (not THEME.fullmatch(value) or value == LIST_NAME):
            problems.append("line %d: theme %r isn't 1-15 of a-z 0-9 _ - (and not %r)" % (number, value, LIST_NAME))
        if not name or len(name) > 47:
            problems.append("line %d: the name is 1-47 characters" % number)
        if len(description) > 191:
            problems.append("line %d: the description is up to 191 characters" % number)
        ids.add(item_id)
        items.append((number, item_id, price, kind, value, name, description))
    if problems:
        sys.exit("%s:\n  %s" % (path, "\n  ".join(problems)))
    return items, raw


def theme_file(themes, theme):
    path = os.path.join(themes, theme + ".theme")
    try:
        data = open(path, "rb").read()
    except FileNotFoundError:
        sys.exit("%s isn't built (a themes/%s/ add-on in app/)" % (path, theme))
    # ThemeHeader: magic, format, then 7 words; ThemeInfo: magic[8], apiVersion, id[16].
    if len(data) < 36 + 28 or struct.unpack_from("<I", data, 0)[0] != THEME_MAGIC:
        sys.exit("%s isn't a .theme file" % path)
    theme_id = data[36 + 12:36 + 28].split(b"\0")[0].decode("ascii", "replace")
    if theme_id != theme:
        sys.exit("%s is theme %r, not %r" % (path, theme_id, theme))
    return data


def main():
    if len(sys.argv) not in (5, 6):
        sys.exit(__doc__)
    header, list_path, themes, out = sys.argv[1:5]
    copy = sys.argv[5] if len(sys.argv) == 6 else None
    aes, mac = store_pack.key_from(header)
    items, raw = read_list(list_path)

    files = {LIST_NAME: raw}
    for _, _, _, kind, value, _, _ in items:
        if kind == "theme":
            files[value] = theme_file(themes, value)

    for name, plain in files.items():
        data = store_pack.pack(aes, mac, name, plain)
        path = os.path.join(out, name + ".bin")
        written = store_pack.write_if_changed(path, data)
        copied = copy is not None and store_pack.write_if_changed(os.path.join(copy, name + ".bin"), data)
        print("%s: %d bytes%s%s" % (path, len(data), "" if written else ", unchanged",
                                    ", copied" if copied else ""))

    for folder in [out] + ([copy] if copy else []):
        for leftover in sorted(os.listdir(folder)):
            if leftover.endswith(".bin") and leftover[:-4] not in files:
                print("note: %s isn't in %s any more (delete it if it's not for sale)" %
                      (os.path.join(folder, leftover), list_path))
    print("store: %d item%s" % (len(items), "" if len(items) == 1 else "s"))


if __name__ == "__main__":
    main()
