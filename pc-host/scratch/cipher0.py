"""The ArchivedFile word cipher (sub_0059c270's mode-0 branch, RVA 0x59c5bb).

Per 0x100-word block:
    w = buf[i] ^ key
    w = permute(w, key & 0xf)        2 -> bswap32, 9 -> (b1,b0,b3,b2)
                                     13 -> rotr16, else unchanged
    buf[i] = w
    key = mix(key)  with mix(k): t = k ^ (k << 8); t ^= t >> 9; k = t ^ (t << 23)

Usage: py cipher0.py <archive.a> <entry-index> [key-hex] [--inflate|--stored]
"""
import struct
import sys
import zlib

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
from arch_decode import fold, inflate, parse, read_chunks  # noqa: E402

M32 = 0xFFFFFFFF


def mix(k):
    t = (k ^ ((k << 8) & M32)) & M32
    t ^= t >> 9
    return (t ^ ((t << 23) & M32)) & M32


def permute(w, sel):
    b = struct.pack("<I", w)
    if sel == 2:
        b = b[::-1]
    elif sel == 9:
        b = bytes((b[1], b[0], b[3], b[2]))
    elif sel == 13:
        b = bytes((b[2], b[3], b[0], b[1]))
    return struct.unpack("<I", b)[0]


def decrypt(data, key):
    buf = bytearray(data)
    nwords = len(buf) // 4
    for i in range(nwords):
        w = struct.unpack_from("<I", buf, i * 4)[0]
        w ^= key
        w = permute(w, key & 0xF)
        struct.pack_into("<I", buf, i * 4, w)
        key = mix(key)
    return bytes(buf), key


def main():
    arc, ent = sys.argv[1], int(sys.argv[2])
    keys = []
    for a in sys.argv[3:]:
        if a.startswith("--"):
            continue
        keys.append(int(a, 16))
    mode_flag = "--inflate" if "--inflate" in sys.argv else (
        "--stored" if "--stored" in sys.argv else None)
    blob, magic, mode, index_off, idx = parse(arc)
    h1, h2, off, size, ck = idx[ent]
    nxt = idx[ent + 1][2] if ent + 1 < len(idx) else index_off
    print("%s e%d mode=%d h=%08x%08x off=%d size=%d span=%d ck=%08x"
          % (arc, ent, mode, h1, h2, off, size, nxt - off, ck))
    if not keys:
        keys = [0x00000000, h1, h2, 1, 0x100]
    if mode_flag == "--inflate":
        chunks = read_chunks(blob, off, nxt)
        data = b"".join(chunks)
        print("  payload=%d chunks=%d" % (len(data), len(chunks)))
    else:
        data = blob[off:off + size]
        print("  stored=%d" % len(data))
    for k in keys:
        out, final = decrypt(data, k)
        if mode_flag == "--inflate":
            try:
                plain = inflate(out)
            except Exception as exc:                   # noqa: BLE001
                print("  key=%08x inflate failed: %s" % (k, exc))
                continue
            got = fold(plain)
            print("  key=%08x plain=%d fold=%08x %s"
                  % (k, len(plain), got, "MATCH" if got == ck else "mismatch"))
        else:
            got = fold(out)
            print("  key=%08x fold=%08x %s%s"
                  % (k, got, "MATCH" if got == ck else "mismatch",
                     "" if got != ck else " (stored)"))
        if got == ck:
            print("  >>> final key state = %08x" % final)


if __name__ == "__main__":
    main()
