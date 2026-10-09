import re
C2 = re.compile(r"/\*\s*([0-9a-f]{8})\s+(.*?)\s*\*/")
for line in ['    /* 0056c7e0  push ebp */\n', '    /* 0056c7e0  push ebp */']:
    print(repr(line), '->', C2.match(line))
lines = open('/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/cpp/isaac/generated/guest_0167.c', 'r', errors='replace').readlines()
for i in range(20765, 20780):
    print(i+1, repr(lines[i][:70]))
