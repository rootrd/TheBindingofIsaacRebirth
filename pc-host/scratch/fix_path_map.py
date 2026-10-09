import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

def R(old, new, n=1):
    global s
    o, w = old.replace('\n', nl), new.replace('\n', nl)
    assert s.count(o) == n, (s.count(o), old[:60])
    s = s.replace(o, w, n)

R('''static signed short g_id_slot[FILE_TOKEN_ID_COUNT];   /* id -> live slot, -1 free */
static int g_id_slot_ready;''',
'''static signed short g_id_slot[FILE_TOKEN_ID_COUNT];   /* id -> live slot, -1 free */
static int g_id_slot_ready;
/* Paths own their id forever (like MSVC's _iob slots, where reopening the
 * same file hands back the same FILE*): the guest frees a stream and keeps
 * using the old value afterwards, which only aliases correctly if a reopen of
 * the same path returns the same token. Hash collisions would alias two
 * streams, so this is a 64-bit hash. */
static struct { uint64_t hash; unsigned char used; } g_path_ids[FILE_TOKEN_ID_COUNT];

static uint64_t file_path_hash(const char *p)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    while (*p != '\\0') {
        h ^= (unsigned char)*p++;
        h *= 0x100000001b3ULL;
    }
    return h;
}''')

R('''            for (tries = 0U; tries < FILE_TOKEN_ID_COUNT; ++tries) {
                unsigned cand = (g_id_scan + tries) % FILE_TOKEN_ID_COUNT;
                if (cand != 0U && g_id_slot[cand] < 0) {
                    id = cand;
                    break;
                }
            }''',
'''            {
                uint64_t h = file_path_hash(host_path != NULL ? host_path : "?");
                for (tries = 0U; tries < FILE_TOKEN_ID_COUNT; ++tries) {
                    if (g_path_ids[tries].used && g_path_ids[tries].hash == h) {
                        id = tries;
                        break;
                    }
                }
                if (id == 0U) {
                    for (tries = 1U; tries < FILE_TOKEN_ID_COUNT; ++tries) {
                        if (!g_path_ids[tries].used && g_id_slot[tries] < 0) {
                            id = tries;
                            g_path_ids[id].used = 1U;
                            g_path_ids[id].hash = h;
                            break;
                        }
                    }
                }
            }''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('path->id map installed')
