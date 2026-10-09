import os, struct, glob
base = "/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed"
for f in sorted(glob.glob(base + "/*.a")):
    d = open(f, "rb").read(64)
    hdr = d[:7]
    mode = d[7]
    idx_off, entries = struct.unpack_from("<IH", d, 8)
    size = os.path.getsize(f)
    ok = "OK " if idx_off == size and hdr == b"ARCH000" else "?? "
    print(f"  {os.path.basename(f):24s} {size:>10d} mode={mode} idx={idx_off:>10d} entries={entries:<5d} {ok}")
