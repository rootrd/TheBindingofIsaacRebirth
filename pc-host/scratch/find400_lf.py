import struct
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
idx_off, entries = struct.unpack_from("<IH", d, 8)
n400 = 0
samples = []
for e in range(entries):
    a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off + e*20)
    l1 = struct.unpack_from("<I", d, off)[0]
    if (l1 & 0x7fffffff) == 0x400:
        n400 += 1
        if len(samples) < 5:
            samples.append((e, hex(a), hex(b), hex(l1), size, hex(ck)))
print(f"entries with first chunk == 0x400: {n400}/{entries}")
for s in samples: print("  ", s)
