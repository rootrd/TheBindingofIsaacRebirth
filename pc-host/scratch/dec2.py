import struct, zlib
M = 0xFFFFFFFF
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
st = open("/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/isaac_state.bin", "rb").read()
index, = struct.unpack_from("<I", st, 0)
r = struct.unpack_from("<256I", st, 4)
print(f"state index={index}")

# entry0: off=14, [u32 313][chunk@18..331]; next chunk [u32 @331][data@335..]
p = 14
clen = struct.unpack_from("<I", d, p)[0]
c1 = d[p+4:p+4+clen]
p = p + 4 + clen
clen2 = struct.unpack_from("<I", d, p)[0]
c2 = d[p+4:p+4+clen2]
print(f"chunk1 len={clen} chunk2 len={clen2} (chunk2 at file {p+4:#x})")

dobj = zlib.decompressobj(-15)
out1 = dobj.decompress(c1)
print("chunk1 out:", len(out1), out1[:30])
ks = struct.pack("<256I", *r[index:]) if index == 0 else struct.pack("<256I", *r)
dec2 = bytes(x ^ y for x, y in zip(c2, ks))
out2 = dobj.decompress(dec2)
print("chunk2 decoded out:", len(out2))
allout = out1 + out2
print("xml context:", allout[1000:1120])
