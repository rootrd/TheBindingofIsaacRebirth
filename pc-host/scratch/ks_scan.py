"""Scan ISAAC keystream start offsets for a captured state against a mode-1 entry.

Generates one long keystream and slides the start index, so the whole scan is a
single O(payload) keystream build instead of one per offset.

Usage: py ks_scan.py <state.bin> <archive.a> <entry-index> [max-skip]
"""
import struct
import sys
import zlib

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
from arch_decode import fold, inflate, parse, read_chunks  # noqa: E402
from isaac_ks import Isaac  # noqa: E402


def main():
    state_path, arc, ent = sys.argv[1], sys.argv[2], int(sys.argv[3])
    max_skip = int(sys.argv[4]) if len(sys.argv) > 4 else 4096
    raw_state = open(state_path, "rb").read()
    blob, magic, mode, index_off, idx = parse(arc)
    h1, h2, off, size, ck = idx[ent]
    nxt = idx[ent + 1][2] if ent + 1 < len(idx) else index_off
    chunks = read_chunks(blob, off, nxt)
    payload = b"".join(chunks)
    print("%s e%d  h=%08x%08x mode=%d chunks=%d payload=%d size=%d ck=%08x"
          % (arc, ent, h1, h2, mode, len(chunks), len(payload), size, ck))

    # One keystream covering every candidate window.
    ks = Isaac(raw_state)
    total = len(payload) + max_skip * 4
    stream = bytearray()
    while len(stream) < total:
        stream += struct.pack("<I", ks.word())
    stream = bytes(stream)

    for skip in range(0, max_skip + 1):
        base = skip * 4
        window = stream[base:base + len(payload)]
        plain = bytes(a ^ b for a, b in zip(payload, window))
        try:
            out = inflate(plain)
        except Exception:                              # noqa: BLE001
            continue
        if len(out) > 8:
            got = fold(out)
            print("  skip=%-5d plain=%-7d inflate OK  fold=%08x  %s"
                  % (skip, len(out), got, "MATCH" if got == ck else "mismatch"))
    print("  scan done")


if __name__ == "__main__":
    main()
