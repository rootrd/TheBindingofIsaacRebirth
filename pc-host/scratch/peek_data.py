import sys
path = "/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile/isaac-ng.exe"
data = open(path, "rb").read()
# .rdata: va=0x606000 raw=0x605000 -> file_off = va - 0x1000
for va in (0x6081d4,):
    off = va - 0x1000
    chunk = data[off:off+64]
    print(hex(va), "file off", hex(off))
    print("bytes:", chunk[:32].hex())
    print("text :", repr(chunk.split(b'\x00')[0].decode('latin1', 'replace')))
# 顺带看周边 128 字节
off = 0x6081d4 - 0x1000 - 48
print("context:", data[off:off+160].replace(b"\x00", b"|").decode("latin1", "replace"))
