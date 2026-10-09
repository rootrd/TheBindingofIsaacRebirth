import struct, zlib
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/animations.a"
d = open(P, "rb").read()
blob = d[14:18] + d[18:18+474]
csize = struct.unpack_from("<I", blob, 0)[0]
print("header dword at 14:", csize, "| bytes at 18:", blob[4:20].hex())
raw = blob[4:4+csize]
print("first bytes:", raw[:16].hex())
for wbits, tag in ((-15, "raw"), (15, "zlib"), (47, "auto")):
    try:
        out = zlib.decompressobj(wbits).decompress(raw)
        print(f"{tag}: inflated {len(out)} bytes head={out[:16].hex()!r}")
        M = 0xFFFFFFFF
        s = 0
        pad = out + b"\0" * ((4 - len(out) % 4) % 4)
        for w in struct.unpack("<%dI" % (len(pad)//4), pad):
            s = (((s << 31) & M) + w + (s >> 1)) & M
        print("   gamehash:", hex(s), "expected: 0x50325a6a")
    except Exception as e:
        print(f"{tag}: FAIL {e}")
