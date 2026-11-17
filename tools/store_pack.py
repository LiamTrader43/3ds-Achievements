"""Encrypts a store item's file (app/source/store_file.h) for /achievements/store/<name>.bin.

    python3 tools/store_pack.py <store_key.h> <name> <file> <out.bin>

The app's Makefile runs it ("make store") from devkitPro's MSYS2 shell. AES-256-CTR comes from
the openssl command; the HMAC-SHA256 from Python. The result is checked by decrypting it again.

Format: "A3SF", u32 format (1), 16-byte IV (random), u32 size, the encrypted file, then the
HMAC-SHA256 (key's last 32 bytes) of the name, a 0 byte and everything before it.
"""
import hashlib
import hmac
import os
import re
import struct
import subprocess
import sys

MAGIC = b"A3SF"
FORMAT = 1


def key_from(header):
    text = open(header, encoding="utf-8").read()
    m = re.search(r'#define\s+STORE_KEY\s+"([0-9a-fA-F]{128})"', text)
    if not m:
        sys.exit("%s has no STORE_KEY of 128 hex digits" % header)
    key = bytes.fromhex(m.group(1))
    return key[:32], key[32:]


def ctr(key, iv, data):
    result = subprocess.run(["openssl", "enc", "-aes-256-ctr", "-K", key.hex(), "-iv", iv.hex(), "-nosalt"],
                            input=data, capture_output=True, check=True)
    return result.stdout


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    header, name, source, out = sys.argv[1:]
    if not re.fullmatch(r"[a-z0-9_-]{1,15}", name):
        sys.exit("a store file's name is 1-15 of a-z 0-9 _ -: %r" % name)
    aes, mac = key_from(header)
    plain = open(source, "rb").read()

    iv = os.urandom(16)
    cipher = ctr(aes, iv, plain)
    if len(cipher) != len(plain):
        sys.exit("openssl gave %d bytes for %d" % (len(cipher), len(plain)))
    body = MAGIC + struct.pack("<I", FORMAT) + iv + struct.pack("<I", len(plain)) + cipher
    tag = hmac.new(mac, name.encode() + b"\0" + body, hashlib.sha256).digest()

    # Check: the tag, and that decrypting gives the file back.
    if ctr(aes, iv, cipher) != plain:
        sys.exit("decrypting it again didn't give the file back")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as f:
        f.write(body + tag)
    print("%s: %d bytes from %s (%d)" % (out, len(body) + len(tag), source, len(plain)))


if __name__ == "__main__":
    main()
