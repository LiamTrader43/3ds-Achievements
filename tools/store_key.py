"""Makes the store's key: app/source/store_key.h (gitignored), 64 random bytes as hex.

    python tools/store_key.py

The first 32 bytes are the AES-256 key the store's files are encrypted with, the last 32 the
HMAC-SHA256 key that proves they weren't changed (app/source/store_file.h). Release builds
compile it in; tools/store_pack.py encrypts with it. Keep a copy somewhere private (never in the
repository): a build with a different key can't open files made with this one. It refuses to
replace a key that's already there.
"""
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
path = os.path.join(here, "..", "app", "source", "store_key.h")

if os.path.exists(path):
    sys.exit("%s is already there: delete it first to make a new key (files made with the old one stop working)" % path)
key = os.urandom(64).hex()
with open(path, "w", newline="\n") as f:
    f.write("// The store's key (tools/store_key.py made it): private. Never commit or share it.\n")
    f.write("// The first 32 bytes encrypt the store's files (AES-256), the last 32 sign them (HMAC-SHA256).\n")
    f.write('#define STORE_KEY "%s"\n' % key)
print("wrote", os.path.normpath(path))
