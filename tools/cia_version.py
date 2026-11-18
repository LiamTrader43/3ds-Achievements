"""Stamps a title version into a CIA's ticket and TMD (what `make cia` runs after makerom).

    python tools/cia_version.py <file.cia> <major.minor.micro>

The 3DS refuses to install a CIA over the same title at the same version ("already exists"),
so every release's CIA needs a higher title version than the one before, or the app can't
update itself as a CIA. makerom can't be trusted with it: devkitPro's v0.19.0 drops the minor
part (-ver 2064 gives 2.0.0, -minor is ignored), and without -ver every CIA was version 0.
So makerom leaves it and this writes the u16 (major << 10 | minor << 4 | micro) into both
the ticket and the TMD, then reads the file back to check. Homebrew CIAs aren't validly
signed anyway (Luma's signature patches let them install), so changing a signed field
breaks nothing; the TMD's content hashes don't cover this field.
"""
import os
import struct
import sys

SIGNATURE_SIZES = {          # signature type -> signature + padding
    0x010003: 0x200 + 0x3C,  # RSA-4096 SHA-256
    0x010004: 0x100 + 0x3C,  # RSA-2048 SHA-256
    0x010005: 0x3C + 0x40,   # ECDSA SHA-256
}
TICKET_TITLE_ID, TICKET_VERSION = 0x9C, 0xA6     # in the ticket's body (3dbrew "Ticket")
TMD_TITLE_ID, TMD_VERSION = 0x4C, 0x9C           # in the TMD's body (3dbrew "Title metadata")


def align64(n):
    return (n + 63) // 64 * 64


def bodies(data):
    """Offsets of the ticket's and the TMD's bodies (just past their signatures)."""
    header_size, _, _, cert_size, ticket_size, tmd_size = struct.unpack_from("<IHHIII", data, 0)
    ticket = align64(header_size) + align64(cert_size)
    tmd = ticket + align64(ticket_size)
    found = []
    for start, size in ((ticket, ticket_size), (tmd, tmd_size)):
        kind = struct.unpack_from(">I", data, start)[0]
        if kind not in SIGNATURE_SIZES or start + size > len(data):
            sys.exit("not a CIA this tool knows (signature type %06X at 0x%X)" % (kind, start))
        found.append(start + 4 + SIGNATURE_SIZES[kind])
    return found


def parse_version(text):
    parts = text.split(".")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        sys.exit("version must be major.minor.micro, like 2.0.1: %r" % text)
    major, minor, micro = map(int, parts)
    if major > 63 or minor > 63 or micro > 15:
        sys.exit("%s doesn't fit a title version (major and minor up to 63, micro up to 15)" % text)
    return major << 10 | minor << 4 | micro


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, version = sys.argv[1], parse_version(sys.argv[2])
    data = bytearray(open(path, "rb").read())
    ticket, tmd = bodies(data)
    ticket_id = struct.unpack_from(">Q", data, ticket + TICKET_TITLE_ID)[0]
    tmd_id = struct.unpack_from(">Q", data, tmd + TMD_TITLE_ID)[0]
    if ticket_id != tmd_id:
        sys.exit("the ticket's title ID %016X isn't the TMD's %016X" % (ticket_id, tmd_id))
    struct.pack_into(">H", data, ticket + TICKET_VERSION, version)
    struct.pack_into(">H", data, tmd + TMD_VERSION, version)

    temporary = path + ".tmp"
    with open(temporary, "wb") as f:
        f.write(data)
    os.replace(temporary, path)

    check = open(path, "rb").read()
    ticket, tmd = bodies(check)
    got = (struct.unpack_from(">H", check, ticket + TICKET_VERSION)[0], struct.unpack_from(">H", check, tmd + TMD_VERSION)[0])
    if got != (version, version):
        sys.exit("reading it back gave %r" % (got,))
    print("%s: title %016X, version %d (%s)" % (os.path.basename(path), tmd_id, version, sys.argv[2]))


if __name__ == "__main__":
    main()
