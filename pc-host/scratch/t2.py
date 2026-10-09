import re
line = open('/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0167.c', 'r', errors='replace').readlines()[20770]
print(repr(line))
C2 = re.compile(r"/\*\s*([0-9a-f]{8})\s+(.*?)\s*\*/")
print('C2', C2.match(line))
