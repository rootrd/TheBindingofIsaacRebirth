import glob, os
inp = open("/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/entry_in.bin", "rb").read()
head = inp[:16]
print("input head:", head.hex())
for p in sorted(glob.glob("/mnt/f/TheBindingofIsaacRebirth/pc-host/build/isaac-data/resources/packed/*.a")):
    d = open(p, "rb").read()
    i = d.find(head)
    if i >= 0:
        name = os.path.basename(p)
        print(f"RAW MATCH: {name} @ {i:#x} ({i})")
        # context: what does the index say about this offset?
