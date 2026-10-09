import struct, zlib
EXE = "/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe"
d = open(EXE, "rb").read()
# section map
e = struct.unpack_from("<I", d, 0x3C)[0]; coff = e + 4
nsec = struct.unpack_from("<H", d, coff + 2)[0]
optsz = struct.unpack_from("<H", d, coff + 16)[0]; opt = coff + 20
secs = []
so = opt + optsz
for k in range(nsec):
    b = so + k * 40
    vsize, va, rsize, raw = struct.unpack_from("<IIII", d, b + 8)
    secs.append((va, max(vsize, rsize), raw, rsize))
def off(rva):
    for va, size, raw, rsize in secs:
        if va <= rva < va + size:
            return raw + (rva - va)
key = d[off(0x7f6140):off(0x7f6140) + 32]
print("template key:", key.hex())

M = 0xFFFFFFFF
def rotl(x, n): return ((x << n) | (x >> (32 - n))) & M
def qr(s, a, b, c, dd):
    s[a] = (s[a] + s[b]) & M; s[dd] = rotl(s[dd] ^ s[a], 16)
    s[c] = (s[c] + s[dd]) & M; s[b] = rotl(s[b] ^ s[c], 12)
    s[a] = (s[a] + s[b]) & M; s[dd] = rotl(s[dd] ^ s[a], 8)
    s[c] = (s[c] + s[dd]) & M; s[b] = rotl(s[b] ^ s[c], 7)
def chacha_block(key, counter, nonce=b"\0"*12):
    st = [0x61707865, 0x3320646e, 0x79622d32, 0x6b206574]
    st += list(struct.unpack("<8I", key))
    st += [counter & M] + list(struct.unpack("<3I", nonce))
    w = st[:]
    for _ in range(10):
        qr(w,0,4,8,12); qr(w,1,5,9,13); qr(w,2,6,10,14); qr(w,3,7,11,15)
        qr(w,0,5,10,15); qr(w,1,6,11,12); qr(w,2,7,8,13); qr(w,3,4,9,14)
    return struct.pack("<16I", *[(w[i] + st[i]) & M for i in range(16)])
def chacha_xor(key, data, counter=0):
    out = bytearray()
    for i in range(0, len(data), 64):
        ks = chacha_block(key, counter + i // 64)
        chunk = data[i:i+64]
        out += bytes(a ^ b for a, b in zip(chunk, ks))
    return bytes(out)

P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/animations.a"
ad = open(P, "rb").read()
idx_off, entries = struct.unpack_from("<IH", ad, 8)
a, b_, o, size, ck = struct.unpack_from("<IIIII", ad, idx_off)
blob = ad[o:idx_off]
print(f"entry: off={o} blob={len(blob)} size_unpacked={size} checksum={ck:08x}")

def gamehash(words):
    s = 0
    for w in words:
        s = (((s << 31) & M) + w + (s >> 1)) & M
    return s

cands = {"raw": blob, "chacha0": chacha_xor(key, blob, 0), "chacha1": chacha_xor(key, blob, 1)}
for name, data in cands.items():
    for tag, fn in (("zlib", lambda x: zlib.decompress(x)), 
                    ("raw-deflate", lambda x: zlib.decompressobj(-15).decompress(x)),
                    ("gzip", lambda x: zlib.decompress(x, 16 + 32))):
        try:
            out = fn(data)
        except Exception:
            continue
        pad = out + b"\0" * ((4 - len(out) % 4) % 4)
        words = list(struct.unpack("<%dI" % (len(pad)//4), pad))
        print(f"  {name}+{tag}: {len(out)} bytes, hash={gamehash(words):08x}, head={out[:8].hex()}")
