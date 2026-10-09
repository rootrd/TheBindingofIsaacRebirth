import struct, zlib
p = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/animations.a"
d = open(p, "rb").read()
print("size", len(d))
print("first 96 bytes:")
for i in range(0, 96, 16):
    print("  %08x  %s  |%s|" % (i, d[i:i+16].hex(" "), "".join(chr(c) if 32 <= c < 127 else "." for c in d[i:i+16])))
# look for a deflate stream that inflates
for i in range(len(d) - 4):
    if d[i] == 0x1f and d[i+1] == 0x8b:
        try:
            out = zlib.decompress(d[i:], 16 + 32)
            print(f"gzip at {i:#x} inflates to {len(out)} bytes; head={out[:16].hex()}")
        except Exception as e:
            pass
print("done")
