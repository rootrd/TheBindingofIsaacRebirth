import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
old = """    if (!g_slots[index].used ||
        (g_slots[index].generation & FILE_TOKEN_GEN_MASK) != generation) {
        return NULL;
    }""".replace('\n', nl)
new = """    if (!g_slots[index].used ||
        (g_slots[index].generation & FILE_TOKEN_GEN_MASK) != generation) {
        FILE_ERR("lookup: token 0x%{public}08x index=%{public}u gen=%{public}u "
                 "slot used=%{public}u gen=%{public}u",
                 token, index, generation, g_slots[index].used,
                 g_slots[index].generation);
        return NULL;
    }""".replace('\n', nl)
assert s.count(old) == 1, s.count(old)
io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(old, new, 1))
print("diagnostic added")
