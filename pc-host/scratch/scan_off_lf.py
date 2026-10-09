import zlib
d = open("/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/animations.a", "rb").read()
blob = d[18:18+474]
ok = []
for start in range(0, 60):
    for wbits, tag in ((-15, "raw"), (15, "zlib")):
        try:
            out = zlib.decompressobj(wbits).decompress(blob[start:])
            ok.append((start, tag, len(out), out[:12].hex()))
        except Exception:
            pass
for o in ok[:10]:
    print(o)
print("successes:", len(ok))
# also try the whole tail of file as one deflate (entry might not be length-prefixed)
for start in (14, 18, 22):
    for wbits, tag in ((-15, "raw"),):
        try:
            out = zlib.decompressobj(wbits).decompress(d[start:660281])
            print("whole-tail", start, tag, len(out), out[:12].hex())
        except Exception as e:
            pass
