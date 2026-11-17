"""Encrypts one of the store's files (app/source/store_file.h) for /achievements/store/<name>.bin.

    python3 tools/store_pack.py <store_key.h> <name> <file> <out.bin>

tools/store_build.py uses it for the whole store ("make store" in app/, from devkitPro's MSYS2
shell). AES-256-CTR comes from the openssl command; the HMAC-SHA256 from Python. The result is
checked by decrypting it again.

Format: "A3SF", u32 format (1), 16-byte IV, u32 size, the encrypted file, then the HMAC-SHA256
(key's last 32 bytes) of the name, a 0 byte and everything before it. The IV is worked out from
the name and the file (an HMAC of them), so the same file always gives the same .bin: a rebuild
doesn't change the copies in the repository, which everyone would download again.
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
NAME = re.compile(r"[a-z0-9_-]{1,15}")


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


def pack(aes, mac, name, plain):
    """The .bin for plain under name (checked by decrypting it again)."""
    if not NAME.fullmatch(name):
        sys.exit("a store file's name is 1-15 of a-z 0-9 _ -: %r" % name)
    iv = hmac.new(mac, b"iv\0" + name.encode() + b"\0" + plain, hashlib.sha256).digest()[:16]
    cipher = ctr(aes, iv, plain)
    if len(cipher) != len(plain):
        sys.exit("openssl gave %d bytes for %d" % (len(cipher), len(plain)))
    if ctr(aes, iv, cipher) != plain:
        sys.exit("decrypting %s again didn't give the file back" % name)
    body = MAGIC + struct.pack("<I", FORMAT) + iv + struct.pack("<I", len(plain)) + cipher
    return body + hmac.new(mac, name.encode() + b"\0" + body, hashlib.sha256).digest()


def write_if_changed(path, data):
    """Writes data to path unless it already holds exactly that. True if it wrote."""
    try:
        with open(path, "rb") as f:
            if f.read() == data:
                return False
    except FileNotFoundError:
        pass
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    return True


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    header, name, source, out = sys.argv[1:]
    aes, mac = key_from(header)
    plain = open(source, "rb").read()
    data = pack(aes, mac, name, plain)
    written = write_if_changed(out, data)
    print("%s: %d bytes from %s (%d)%s" % (out, len(data), source, len(plain), "" if written else ", unchanged"))


if __name__ == "__main__":
    main()
