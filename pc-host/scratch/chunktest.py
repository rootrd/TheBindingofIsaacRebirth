import struct, zlib
M = 0xFFFFFFFF
def gamehash(data):
    s = 0
    pad = data + b"\0" * ((4 - len(data) % 4) % 4)
    for w in struct.unpack("<%dI" % (len(pad)//4), pad):
        s = (((s << 31) & M) + w + (s >> 1)) & M
    return s
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
idx_off, entries = struct.unpack_from("<IH", d, 8)
a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off)
pos = off
dobj = zlib.decompressobj(-15)
total = b""
chunks = 0
while len(total) < size and pos + 4 <= idx_off:
    clen = struct.unpack_from("<I", d, pos)[0]
    pos += 4
    chunk = d[pos:pos+clen]
    pos += clen
    total += dobj.decompress(chunk)
    chunks += 1
    if clen == 0:
        break
print(f"chunks={chunks} total={len(total)} want={size}")
if len(total) >= size:
    total = total[:size]
    h = gamehash(total)
    print(f"hash={h:08x} want={ck:08x} {'*** MATCH ***' if h == ck else 'MISMATCH'}")
    print("head:", total[:40])
