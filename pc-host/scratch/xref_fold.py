"""Cross-reference the guest's fold probes against the archives.

For every (h1,h2) the game tried to load, find the index entry, decode it in
Python with the verified model, and compare our fold with the expected checksum
the game holds. A match on a PLAIN entry while the guest's own staging buffer
was all zeros proves the guest's decompression is what is broken, not the
crypto.

Usage: py xref_fold.py <packed-dir> <fold-log>
"""
import glob
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from arch_decode import fold, inflate, read_chunks, FOLD_INIT  # noqa: E402

RE_FOLD = re.compile(r"h1=([0-9a-f]{8}) h2=([0-9a-f]{8}) off=([0-9a-f]{8}) "
                     r"size=([0-9a-f]{8}) expect=([0-9a-f]{8}) buf=([0-9a-f]*)")


def build_index(packed):
    """(h1,h2) -> (archive, entry_index, blob, index_off, entry_count)."""
    table = {}
    for path in glob.glob(os.path.join(packed, "*.a")):
        with open(path, "rb") as fh:
            blob = fh.read()
        if blob[:7] != b"ARCH000":
            continue
        mode = blob[7]
        index_off, entries = struct.unpack_from("<IH", blob, 8)
        for i in range(entries):
            h1, h2, off, size, ck = struct.unpack_from("<IIIII", blob, index_off + i * 20)
            table.setdefault((h1, h2), (os.path.basename(path), mode, i, blob, index_off, ck))
    return table


def main():
    packed, log = sys.argv[1], sys.argv[2]
    table = build_index(packed)
    print("index: %d archives' worth of hashes" % len(table))
    with open(log, "rb") as fh:
        text = fh.read().decode("latin1")
    seen = set()
    stats = {"plain-match": 0, "plain-mismatch": 0, "cipher": 0, "unknown": 0}
    for m in RE_FOLD.finditer(text):
        h1, h2 = int(m.group(1), 16), int(m.group(2), 16)
        expect = int(m.group(5), 16)
        guest_buf = m.group(6)
        if (h1, h2) in seen:
            continue
        seen.add((h1, h2))
        hit = table.get((h1, h2))
        if hit is None:
            stats["unknown"] += 1
            print("%08x%08x  NOT FOUND in any index" % (h1, h2))
            continue
        name, mode, i, blob, index_off, ck = hit
        end = index_off
        # next entry's offset bounds the chunk stream
        if i + 1 < struct.unpack_from("<H", blob, 12)[0]:
            nxt = struct.unpack_from("<I", blob, index_off + (i + 1) * 20 + 8)[0]
            if nxt > 0:
                end = nxt
        off = struct.unpack_from("<I", blob, index_off + i * 20 + 8)[0]
        chunks = read_chunks(blob, off, end)
        enc = sum(1 for ch in chunks if len(ch) == 0x400)
        head = "%-16s e%-5d mode=%d enc=%-4d" % (name, i, mode, enc)
        if enc:
            stats["cipher"] += 1
            print("%s expect=%08x  (cipher chunks - needs the keystream)" % (head, expect))
            continue
        try:
            plain = inflate(b"".join(chunks))
        except Exception as exc:                       # noqa: BLE001
            print("%s expect=%08x  inflate failed: %s" % (head, expect, exc))
            continue
        mine = fold(plain)
        ok = (mine == expect) and (mine == ck)
        stats["plain-match" if ok else "plain-mismatch"] += 1
        print("%s expect=%08x python=%08x index=%08x %s  guestbuf=%s"
              % (head, expect, mine, ck, "MATCH" if ok else "MISMATCH",
                 "zeros" if set(guest_buf) <= {"0"} else guest_buf[:16]))
    print("--- %s ---" % stats)


if __name__ == "__main__":
    main()
