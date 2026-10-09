import struct, zlib
M = 0xFFFFFFFF
P = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/afterbirth.a"
d = open(P, "rb").read()
mode = d[7]
idx_off, entries = struct.unpack_from("<IH", d, 8)
print(f"mode={mode} idx_off={idx_off} entries={entries}")
a, b, off, size, ck = struct.unpack_from("<IIIII", d, idx_off)
print(f"entry0: name={a:08x}{b:08x} off={off} size={size} ck={ck:08x}")
csize = struct.unpack_from("<I", d, off)[0]
print(f"u32 @off = {csize}")
blob = d[off+4:off+4+min(csize, 4096)]
print("blob head:", blob[:16].hex())
inp = open("/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/entry_in.bin", "rb").read()
print("dumped input == blob?", blob[:1024] == inp[:1024] if csize >= 1024 else blob == inp[:len(blob)])
for wbits, tag in ((-15, "raw"), (15, "zlib"), (47, "auto")):
    try:
        out = zlib.decompressobj(wbits).decompress(blob)
        print(f"{tag}: inflated {len(out)} bytes head={out[:16].hex()}")
        break
    except Exception as e:
        print(f"{tag}: fail {e}")
