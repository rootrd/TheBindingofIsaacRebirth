import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

def R(old, new, n=1):
    global s
    o, w = old.replace('\n', nl), new.replace('\n', nl)
    assert s.count(o) == n, (s.count(o), old[:60])
    s = s.replace(o, w, n)

R('''static file_slot g_slots[FILE_SLOT_COUNT];
static signed short g_id_slot[FILE_TOKEN_ID_COUNT];   /* id -> live slot, -1 free */''',
'''static file_slot g_slots[FILE_SLOT_COUNT];
static signed short g_id_slot[FILE_TOKEN_ID_COUNT];   /* id -> live slot, -1 free */
static int g_id_slot_ready;''')

R('''            {
                unsigned tries;
                unsigned id = 0U;''',
'''            {
                unsigned tries;
                unsigned id = 0U;
                if (!g_id_slot_ready) {
                    unsigned k;
                    for (k = 0U; k < FILE_TOKEN_ID_COUNT; ++k) {
                        g_id_slot[k] = -1;
                    }
                    g_id_slot_ready = 1;
                }''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('id table lazily initialized')
