import struct, zlib
M = 0xFFFFFFFF
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
idx_off, entries = struct.unpack_from("<IH", d, 8)
a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off)
pos = off
dobj = zlib.decompressobj(-15)
total = b""
lens = []
while len(total) < size and pos + 4 <= idx_off:
    clen = struct.unpack_from("<I", d, pos)[0]
    lens.append(clen)
    pos += 4
    total += dobj.decompress(d[pos:pos+clen])
    pos += clen
print("entry0 chunk lens:", lens[:45])
print("full-0x400 chunks:", sum(1 for L in lens if (L & 0x7fffffff) == 0x400))
def fold(data, be=False, init=0):
    s = init
    pad = data + b"\0" * ((4 - len(data) % 4) % 4)
    fmt = ">%dI" if be else "<%dI"
    for w in struct.unpack(fmt % (len(pad)//4), pad):
        s = (((s << 31) & M) + ((w + (s >> 1)) & M)) & M
    return s
print("fold LE full:", hex(fold(total)), " BE full:", hex(fold(total, be=True)))
print("fold LE 512:", hex(fold(total[:512])), " BE 512:", hex(fold(total[:512], be=True)))
# try adler32/crc32 too
print("adler32:", hex(zlib.adler32(total)), "crc32:", hex(zlib.crc32(total) & M))
print("target:", hex(ck))
