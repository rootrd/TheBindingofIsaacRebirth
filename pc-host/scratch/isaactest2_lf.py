import struct, zlib
M = 0xFFFFFFFF
class Isaac:
    def __init__(self, seed):
        mm = self.mm = [0]*256
        rr = self.r = [0]*256
        rr[0] = seed            # randrsl[0] = seed, rest 0
        x = [0x9e3779b9] * 8
        # canonical readable.c mix:
        def mixr(m):
            m[0] ^= (m[1] << 11) & M; m[3] ^= m[2]; m[5] ^= (m[4] << 2) & M
            m[7] ^= m[6]; m[6] ^= (m[5] >> 3) & M; m[4] ^= (m[5] << 9) & M
            m[2] ^= (m[3] >> 5) & M; m[0] ^= (m[7] << 7) & M
            m[3] ^= (m[0] >> 16) & M; m[5] ^= (m[2] >> 4) & M
            m[7] ^= (m[4] >> 7) & M; m[6] ^= (m[1] << 3) & M
            m[1] ^= (m[0] >> 10) & M; m[2] ^= (m[3] << 15) & M
            m[4] ^= (m[5] >> 12) & M; m[7] ^= (m[6] << 2) & M
            m[6] ^= (m[7] >> 13) & M; m[4] ^= (m[0] << 8) & M
            m[1] ^= (m[2] >> 6) & M; m[3] ^= (m[5] << 5) & M
            m[5] ^= (m[6] >> 15) & M; m[7] ^= (m[0] >> 9) & M
            m[0] ^= m[1]
        for _ in range(4):
            mixr(x)
        for i in range(0, 256, 8):
            x[0] = (x[0] + rr[i]) & M
            x[1] = (x[1] + rr[i+1]) & M
            x[2] = (x[2] + rr[i+2]) & M
            x[3] = (x[3] + rr[i+3]) & M
            x[4] = (x[4] + rr[i+4]) & M
            x[5] = (x[5] + rr[i+5]) & M
            x[6] = (x[6] + rr[i+6]) & M
            x[7] = (x[7] + rr[i+7]) & M
            mixr(x)
            for j in range(8):
                mm[i+j] = x[j]
        self.a = self.b = self.c = 0
        self.cnt = 0
        self.isaa()

    def isaa(self):
        mm = self.mm
        self.c = (self.c + 1) & M
        b = (self.b + self.c) & M
        a = self.a
        r = self.r
        for i in range(0, 256, 4):
            x = mm[i]
            a = (a ^ ((a << 13) & M)) & M
            a = (a + mm[(i + 128) & 255]) & M
            y = (mm[(x >> 2) & 255] + a + b) & M
            mm[i] = y
            b = (mm[(y >> 10) & 255] + x) & M
            r[i] = b
            x = mm[i+1]
            a = (a ^ (a >> 6)) & M
            a = (a + mm[(i + 129) & 255]) & M
            y = (mm[(x >> 2) & 255] + a + b) & M
            mm[i+1] = y
            b = (mm[(y >> 10) & 255] + x) & M
            r[i+1] = b
            x = mm[i+2]
            a = (a ^ ((a << 2) & M)) & M
            a = (a + mm[(i + 130) & 255]) & M
            y = (mm[(x >> 2) & 255] + a + b) & M
            mm[i+2] = y
            b = (mm[(y >> 10) & 255] + x) & M
            r[i+2] = b
            x = mm[i+3]
            a = (a ^ (a >> 16)) & M
            a = (a + mm[(i + 131) & 255]) & M
            y = (mm[(x >> 2) & 255] + a + b) & M
            mm[i+3] = y
            b = (mm[(y >> 10) & 255] + x) & M
            r[i+3] = b
        self.a = a
        self.b = b

    def keystream(self, n_words):
        out = []
        while len(out) < n_words:
            out += self.r[self.cnt:]
            self.cnt = 256
            self.isaa()
            self.cnt = 0
        return out[:n_words]

def fold(data, init=0xababeb98):
    s = init
    pad = data + b"\0" * ((4 - len(data) % 4) % 4)
    for w in struct.unpack("<%dI" % (len(pad)//4), pad):
        s = (((s << 31) & M) + ((w + (s >> 1)) & M)) & M
    return s

P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
idx_off, entries = struct.unpack_from("<IH", d, 8)

def decode_entry(off, size, seed):
    pos = off
    total = b""
    ich = 0
    while len(total) < size:
        v = struct.unpack_from("<I", d, pos)[0]
        ln = v & 0x7fffffff
        enc = (v & 0x7fffffff) == 0x400
        pos += 4
        chunk = d[pos:pos+ln]
        pos += ln
        if enc:
            ks = Isaac(seed).keystream(ln // 4)
            ksb = struct.pack("<%dI" % len(ks), *ks)
            chunk = bytes(x ^ y for x, y in zip(chunk, ksb))
        total += chunk
        ich += 1
        if v & 0x80000000:
            break
    return total

for e, nw in ((13, 0x2de715be),):
    a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off + e*20)
    for seed in (b, a):
        try:
            out = decode_entry(off, size, seed)
            h = fold(out[:size])
            print(f"e{e} seed={seed:08x}: len={len(out)} fold={h:08x} want={ck:08x} {'*** MATCH ***' if h == ck else ''}")
            print("   head:", out[:24].hex())
        except Exception as ex:
            print(f"e{e} seed={seed:08x}: error {ex}")
