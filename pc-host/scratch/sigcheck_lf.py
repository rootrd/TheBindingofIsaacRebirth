import os
base = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed"
sigs = {b"\x89PNG\r\n\x1a\n": "PNG", b"OggS": "OGG", b"RIFF": "RIFF/WAV",
        b"DDS ": "DDS", b"PK\x03\x04": "ZIP", b"\x1f\x8b": "GZIP"}
for name in ("animations.a", "graphics.a", "resources.a"):
    p = os.path.join(base, name)
    if not os.path.exists(p):
        print(name, "missing"); continue
    d = open(p, "rb").read()
    print(f"== {name}  {len(d)} bytes")
    for s, label in sigs.items():
        print(f"   {label:9s} {d.count(s)}")
    # plaintext filenames?
    for probe in (b"gfx/", b"resources/", b".png", b".ogg", b".anm2"):
        print(f"   text {probe!r:14s} {d.count(probe)}")
