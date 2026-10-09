"""ISAAC keystream test against the live captured state.

sub_005c2fd0 (the refresh) is Bob Jenkins' isaac() verbatim; state layout is
  +0x000 randcnt, +0x004 r[256], +0x404 m[256], +0x804 a, +0x808 b, +0x80c c.
The entry decoder sub_005b06f0 consumes one word per 4 payload bytes, bumps
randcnt, and refreshes when it wraps past 256.

Usage: py isaac_ks.py <state.bin> <archive.a> <entry-index>
"""
import struct
import sys
import zlib

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
from arch_decode import fold, inflate, parse, read_chunks  # noqa: E402

M32 = 0xFFFFFFFF


class Isaac:
    def __init__(self, blob):
        self.st = bytearray(blob[:0x810])
        self.index = self.u32(0)

    def u32(self, off):
        return struct.unpack_from("<I", self.st, off)[0]

    def setu32(self, off, v):
        struct.pack_into("<I", self.st, off, v & M32)

    def refresh(self):
        st = self.st
        c = (self.u32(0x80C) + 1) & M32
        self.setu32(0x80C, c)
        b = (self.u32(0x808) + c) & M32
        self.setu32(0x808, b)
        a = self.u32(0x804)
        mm = 0x404
        for i in range(256):
            x = self.u32(mm + i * 4)
            m = i & 3
            if m == 0:
                a ^= (a << 13) & M32
            elif m == 1:
                a ^= a >> 6
            elif m == 2:
                a ^= (a << 2) & M32
            else:
                a ^= a >> 16
            a = (a + self.u32(mm + ((i + 128) & 255) * 4)) & M32
            y = (self.u32(mm + ((x >> 2) & 255) * 4) + a + b) & M32
            self.setu32(mm + i * 4, y)
            b = (self.u32(mm + ((y >> 10) & 255) * 4) + x) & M32
            self.setu32(4 + i * 4, b)
        self.setu32(0x804, a)
        self.setu32(0x808, b)

    def word(self):
        if self.index >= 256:
            self.refresh()
            self.index = 0
        w = self.u32(4 + self.index * 4)
        self.index += 1
        self.setu32(0, self.index)
        return w

    def keystream(self, n):
        return b"".join(struct.pack("<I", self.word()) for _ in range((n + 3) // 4))


def main():
    state_path, arc, ent = sys.argv[1], sys.argv[2], int(sys.argv[3])
    raw_state = open(state_path, "rb").read()
    blob, magic, mode, index_off, idx = parse(arc)
    h1, h2, off, size, ck = idx[ent]
    nxt = idx[ent + 1][2] if ent + 1 < len(idx) else index_off
    chunks = read_chunks(blob, off, nxt)
    payload = b"".join(chunks)
    print("%s e%d  h=%08x%08x  mode=%d  chunks=%d  payload=%d  expect=%08x"
          % (arc, ent, h1, h2, mode, len(chunks), len(payload), ck))

    for label, skip in (("full", 0), ("skip4", 4), ("skip8", 8)):
        ks = Isaac(raw_state)
        if skip:
            ks.keystream(skip)
        ks_stream = ks.keystream(len(payload))
        plain = bytes(a ^ b for a, b in zip(payload, ks_stream))
        try:
            out = inflate(plain)
        except Exception as exc:                       # noqa: BLE001
            print("  %-6s inflate failed: %s" % (label, exc))
            continue
        got = fold(out)
        print("  %-6s plain=%d fold=%08x %s"
              % (label, len(out), got, "MATCH" if got == ck else "mismatch"))


if __name__ == "__main__":
    main()
