import struct, zlib
M = 0xFFFFFFFF
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
idx_off, entries = struct.unpack_from("<IH", d, 8)
a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off)
pos = off
dobj = zlib.decompressobj(-15)
total = b""
while len(total) < size and pos + 4 <= idx_off:
    clen = struct.unpack_from("<I", d, pos)[0]
    pos += 4
    total += dobj.decompress(d[pos:pos+clen])
    pos += clen
def fold(data, init=0):
    s = init
    pad = data + b"\0" * ((4 - len(data) % 4) % 4)
    for w in struct.unpack("<%dI" % (len(pad)//4), pad):
        s = (((s << 31) & M) + ((w + (s >> 1)) & M)) & M
    return s
print("target ck = %08x, out len = %d" % (ck, len(total)))
for n in (512, 1024, 2048, len(total)):
    for init in (0, size, M, 0x12345678):
        h = fold(total[:n], init)
        if h == ck:
            print(f"*** MATCH n={n} init={init:#x}")
print("512/0:", hex(fold(total[:512])), " full/0:", hex(fold(total)))
