import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

def R(old, new, count=1):
    global s
    o = old.replace('\n', nl)
    n = new.replace('\n', nl)
    assert s.count(o) == count, (s.count(o), old.split('\n')[0][:60])
    s = s.replace(o, n, count)

R("""#define FILE_SLOT_COUNT 64U""",
  """/* The guest keeps many streams open at once (the archive loader alone opens
 * hundreds), and a slot is only recycled after a close, so 64 slots recycled
 * constantly. 1024 keeps _fileno (= index + 3) in range and cuts the recycle
 * pressure; the generation field below is 8 bits wide. */
#define FILE_SLOT_COUNT 1024U""")

R("""#define FILE_TOKEN_INDEX_MASK (FILE_SLOT_COUNT - 1U)
#define FILE_TOKEN_GEN_MASK 0xFFFU""",
  """#define FILE_TOKEN_INDEX_BITS 10U
#define FILE_TOKEN_INDEX_MASK (FILE_SLOT_COUNT - 1U)
#define FILE_TOKEN_GEN_MASK ((FILE_TOKEN_SPAN >> FILE_TOKEN_INDEX_BITS) - 1U)""")

R("""    return FILE_TOKEN_BASE | ((generation & FILE_TOKEN_GEN_MASK) << 6) |
           (uint32_t)index;""",
  """    return FILE_TOKEN_BASE | ((generation & FILE_TOKEN_GEN_MASK) << FILE_TOKEN_INDEX_BITS) |
           (uint32_t)index;""")

R("""    generation = (rel >> 6) & FILE_TOKEN_GEN_MASK;
    if (!g_slots[index].used || g_slots[index].generation != generation) {""",
  """    generation = (rel >> FILE_TOKEN_INDEX_BITS) & FILE_TOKEN_GEN_MASK;
    /* The stored counter is only truncated when it is stamped into a token, so
     * comparing it unmasked made every token of a slot recycled past the end of
     * the generation field invalid the moment it was issued: the guest saw a
     * fresh fopen whose fseek/fread all failed and every archive entry read
     * back as zeros ("checksum is invalid"). */
    if (!g_slots[index].used ||
        (g_slots[index].generation & FILE_TOKEN_GEN_MASK) != generation) {""")

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print("file layer patched (nl=%r)" % nl)
