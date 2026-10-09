import struct, zlib
st = open("/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/isaac_state.bin", "rb").read()
inp = open("/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/entry_in.bin", "rb").read()
index, = struct.unpack_from("<I", st, 0)
r = struct.unpack_from("<256I", st, 4)
mm = struct.unpack_from("<256I", st, 0x404)
a, b, c = struct.unpack_from("<3I", st, 0x804)
print(f"index={index} a={a:08x} b={b:08x} c={c} r0={r[0]:08x} mm0={mm[0]:08x}")
print("input head:", inp[:24].hex())
# 1) does the raw input inflate?
for wbits, tag in ((-15, "raw"), (15, "zlib")):
    try:
        out = zlib.decompressobj(wbits).decompress(inp)
        print("RAW inflate:", len(out), out[:16].hex())
    except Exception as e:
        print(f"raw {tag}: fail")
# 2) XOR with r[] from current index
M = 0xFFFFFFFF
def try_xor(words, label):
    ks = struct.pack("<%dI" % len(words), *words)
    dec = bytes(x ^ y for x, y in zip(inp, ks))
    for wbits, tag in ((-15, "raw"), (15, "zlib")):
        try:
            out = zlib.decompressobj(wbits).decompress(dec)
            print(f"{label} {tag}: INFLATED {len(out)} bytes head={out[:24].hex()}")
            return True
        except Exception:
            pass
    print(f"{label}: head after xor = {dec[:16].hex()}")
    return False
try_xor(r[index:], "r[index:]")
try_xor(r, "r[0:]")
