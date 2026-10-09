import io, re

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

# ---- 1) locate and remove the whole current token machinery region --------
# from the FILE_SLOT_COUNT define block to just before file_slot_get's body end
start = s.index('#define FILE_SLOT_COUNT')
end_marker = 'static file_slot *file_slot_get'
end = s.index(end_marker)

new_block = '''#define FILE_SLOT_COUNT 1024U
#define FILE_TOKEN_BASE 0x00050000U
#define FILE_TOKEN_SPAN (1U << 18)
#define FILE_TOKEN_MAX (FILE_TOKEN_SPAN - 1U)
/* _fileno() reports index + 3, leaving 0..2 to the standard streams. */
#define FILE_FD_BASE 3
/* ... and the standard streams live at IOB_BASE (its IOB_BASE). The guest may
 * legitimately pass one of those three to _fileno/fflush/fwrite/fclose, so
 * they are recognised here and mapped onto the host's own standard streams. */
#define FILE_IOB_BASE 0x00010000U
#define FILE_IOB_COUNT 3U

typedef struct {
    FILE    *fp;                        /* native stream, host side only */
    uint32_t token;                     /* the token bound to this slot */
    uint8_t  used;
    uint8_t  is_standard;               /* one of the three standard streams */
    uint8_t  read_seen, write_seen;     /* first-call logging, per stream */
    char     path[FILE_PATH_MAX];       /* host path, for the log */
} file_slot;

static file_slot g_slots[FILE_SLOT_COUNT];
static uint32_t g_open_serial;

/* token = FILE_TOKEN_BASE + t. t values are handed out lowest-free-first and
 * recycled on close, which reproduces the MSVC CRT's _iob slot semantics the
 * game was compiled against: FILE* is a pointer into a small recycled pool,
 * reopening a closed file usually hands back the same value, and a stale
 * FILE* kept by the guest aliases whichever stream now owns that pool slot.
 * Schemes that minted unique-per-open values (generation fields, id pools)
 * broke every pattern where the guest frees a stream and keeps using the old
 * value -- which the archive loader does constantly. */
static signed short g_tok_slot[FILE_TOKEN_SPAN];      /* t -> slot, -1 free */
static int g_tok_ready;
static unsigned g_tok_scan = 1U;

static uint32_t file_token_make(unsigned index, uint32_t token)
{
    (void)index;
    return token;
}

'''.replace('\n', nl)

s = s[:start] + new_block + s[end:]

# ---- 2) rewrite file_slot_get ---------------------------------------------
gstart = s.index('static file_slot *file_slot_get')
gend = s.index('/* Bridge for sibling modules', gstart)
new_get = '''static file_slot *file_slot_get(uint32_t token, unsigned *index_out)
{
    uint32_t t;
    signed short slot;

    if (token < FILE_TOKEN_BASE) {
        return NULL;
    }
    t = token - FILE_TOKEN_BASE;
    if (t >= FILE_TOKEN_SPAN) {
        return NULL;
    }
    if (!g_tok_ready) {
        return NULL;
    }
    slot = g_tok_slot[t];
    if (slot < 0 || !g_slots[slot].used) {
        return NULL;
    }
    if (index_out != NULL) {
        *index_out = (unsigned)slot;
    }
    return &g_slots[slot];
}

'''.replace('\n', nl)
s = s[:gstart] + new_get + s[gend:]

# ---- 3) rewrite file_slot_alloc -------------------------------------------
astart = s.index('static int file_slot_alloc')
aend = s.index('static void file_slot_release', astart)
new_alloc = '''static int file_slot_alloc(FILE *fp, const char *host_path, int is_standard,
                           uint32_t *token_out)
{
    unsigned i;
    unsigned t;
    unsigned tries;

    if (!g_tok_ready) {
        unsigned k;
        for (k = 0U; k < FILE_TOKEN_SPAN; ++k) {
            g_tok_slot[k] = -1;
        }
        g_tok_ready = 1;
    }
    for (i = 0U; i < FILE_SLOT_COUNT; ++i) {
        if (!g_slots[i].used) {
            break;
        }
    }
    if (i == FILE_SLOT_COUNT) {
        FILE_ERR("fopen: all %u streams are in use; \\"%{public}s\\" not opened",
                 FILE_SLOT_COUNT, host_path != NULL ? host_path : "?");
        return -1;
    }
    for (tries = 0U; tries <= FILE_TOKEN_MAX; ++tries) {
        t = (g_tok_scan + tries) % FILE_TOKEN_SPAN;
        if (t != 0U && g_tok_slot[t] < 0) {
            break;
        }
    }
    if (t == 0U && g_tok_slot[0] >= 0) {
        FILE_ERR("fopen: out of tokens; \\"%{public}s\\" not opened",
                 host_path != NULL ? host_path : "?");
        return -1;
    }
    {
        file_slot *slot = &g_slots[i];
        slot->token = FILE_TOKEN_BASE + t;
        slot->fp = fp;
        slot->is_standard = (uint8_t)(is_standard != 0);
        slot->read_seen = 0U;
        slot->write_seen = 0U;
        slot->path[0] = 0;
        if (host_path != NULL) {
            snprintf(slot->path, sizeof slot->path, "%s", host_path);
        }
        slot->used = 1U;
        g_tok_slot[t] = (signed short)i;
        g_tok_scan = (t + 1U) % FILE_TOKEN_SPAN;
        *token_out = slot->token;
        file_history_note(*token_out, host_path, 'O');
    }
    return 0;
}

'''.replace('\n', nl)
s = s[:astart] + new_alloc + s[aend:]

# ---- 4) rewrite file_slot_release -----------------------------------------
rstart = s.index('static void file_slot_release')
rend = s.index('/* ----------------------------------------------------------------', rstart)
new_rel = '''static void file_slot_release(file_slot *slot)
{
    if (slot->token >= FILE_TOKEN_BASE && slot->token < FILE_TOKEN_BASE + FILE_TOKEN_SPAN) {
        file_history_note(slot->token, slot->path, 'C');
        g_tok_slot[slot->token - FILE_TOKEN_BASE] = -1;
    }
    slot->token = 0U;
    slot->fp = NULL;
    slot->used = 0U;
    slot->is_standard = 0U;
    slot->path[0] = '\\0';
}

'''.replace('\n', nl)
s = s[:rstart] + new_rel + s[rend:]

# ---- 5) drop the leftover debug probes anywhere else -----------------------
lines = s.split(nl)
out = [ln for ln in lines
       if 'get: below base' not in ln and 'get: outside span' not in ln
       and 'get: raw g_id_slot' not in ln and 'alloc: bound id' not in ln
       and 'get: 0x' not in ln]
s = nl.join(out)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('iob-style token recycling installed')
