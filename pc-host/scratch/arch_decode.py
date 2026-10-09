"""Ground-truth decode test for the ISAAC archive container.

Model under test (2026-09-20 crack, refined):
  header : 'ARCH000'(7) mode(1) index_offset(u32) entries(u16)
  data   : entries live in [14, index_offset), each is a chunk stream
           [u32 len][len bytes] ... ; len bit31 = last chunk of the entry.
  index  : at index_offset, 20 bytes/entry:
           hash1(u32) hash2(u32) offset(u32) size(u32) checksum(u32)
           (size = the UNCOMPRESSED length)
  fold   : s = rotr(s, 1) + w  (u32), init 0xababeb98, over the plaintext,
           accumulated in 0x200-byte blocks whose tail is ZERO PADDED to a
           whole word, so a plaintext length that is not a multiple of 4 still
           folds its trailing bytes in.

Usage: py arch_decode.py <archive.a> [max_entries]
"""
import struct
import sys
import zlib

FOLD_INIT = 0xABABEB98
CHUNK_CIPHER_LEN = 0x400


def fold(data, init=FOLD_INIT):
    """The guest memsets its 0x200-byte staging buffer ONCE, outside the block
    loop, then reads each block over it and folds ceil(bytes/4) words. So a
    block whose length is not a multiple of 4 folds its final short word using
    the PREVIOUS block's stale bytes, not zeros. Reproduce the buffer exactly:
    that stale tail is worth 24 bits of the checksum."""
    s = init
    buf = bytearray(0x200)
    for base in range(0, len(data), 0x200):
        block = data[base:base + 0x200]
        buf[0:len(block)] = block
        for i in range((len(block) + 3) // 4):
            w = struct.unpack_from("<I", buf, i * 4)[0]
            s = ((((s << 31) & 0xFFFFFFFF) | (s >> 1)) + w) & 0xFFFFFFFF
    return s


def parse(path):
    with open(path, "rb") as fh:
        blob = fh.read()
    magic = blob[:7]
    mode = blob[7]
    index_off, entries = struct.unpack_from("<IH", blob, 8)
    idx = []
    for i in range(entries):
        h1, h2, off, size, ck = struct.unpack_from("<IIIII", blob, index_off + i * 20)
        idx.append((h1, h2, off, size, ck))
    return blob, magic, mode, index_off, idx


def read_chunks(blob, off, end):
    """[u32 len][len bytes] until the last-chunk bit or `end` is reached."""
    chunks = []
    p = off
    while p + 4 <= end:
        (raw,) = struct.unpack_from("<I", blob, p)
        p += 4
        n = raw & 0x7FFFFFFF
        last = bool(raw & 0x80000000)
        chunks.append(blob[p:p + n])
        p += n
        if last:
            break
    return chunks


def inflate(blob):
    d = zlib.decompressobj(-15)
    out = d.decompress(blob)
    out += d.flush()
    return out


def main():
    path = sys.argv[1]
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    blob, magic, mode, index_off, idx = parse(path)
    print("file   : %s" % path)
    print("magic  : %r  mode=%d  index_off=%d  entries=%d  size=%d"
          % (magic, mode, index_off, len(idx), len(blob)))
    ok = bad = cipher = 0
    for i, (h1, h2, off, size, ck) in enumerate(idx[:limit]):
        end = idx[i + 1][2] if i + 1 < len(idx) else index_off
        chunks = read_chunks(blob, off, end)
        enc = sum(1 for ch in chunks if len(ch) == CHUNK_CIPHER_LEN)
        raw = b"".join(chunks)
        row = "e%-4d %08x%08x off=%-9d chunks=%-5d enc=%-4d rawlen=%-7d" % (
            i, h1, h2, off, len(chunks), enc, len(raw))
        if enc:
            cipher += 1
            print(row + " (cipher chunks present - skipped)")
            continue
        try:
            plain = inflate(raw)
        except Exception as exc:                       # noqa: BLE001
            print(row + " INFLATE FAIL %s" % exc)
            bad += 1
            continue
        got = fold(plain)
        if got == ck:
            ok += 1
            print(row + " plain=%-7d FOLD OK" % len(plain))
        else:
            bad += 1
            print(row + " plain=%-7d FOLD %08x != %08x size_field=%d"
                  % (len(plain), got, ck, size))
    print("--- ok=%d bad=%d cipher=%d (of %d shown) ---"
          % (ok, bad, cipher, min(limit, len(idx))))


if __name__ == "__main__":
    main()
