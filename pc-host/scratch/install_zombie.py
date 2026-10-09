import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

def R(old, new, n=1):
    global s
    o, w = old.replace('\n', nl), new.replace('\n', nl)
    assert s.count(o) == n, (s.count(o), old[:70])
    s = s.replace(o, w, n)

# struct: add zombie flag
R('''typedef struct {
    FILE    *fp;                        /* native stream, host side only */
    uint32_t token;                     /* the token bound to this slot */
    uint8_t  used;''',
'''typedef struct {
    FILE    *fp;                        /* native stream, host side only */
    uint32_t token;                     /* the token bound to this slot */
    uint8_t  used;
    uint8_t  zombie;                    /* closed, but the guest may still
                                         * hold and use the old FILE* value:
                                         * MSVC's freed _iob slot tolerates
                                         * that, so ops on a zombie are
                                         * benign no-ends (write=ok,
                                         * read=0, seek=0) until the slot
                                         * is reopened. */''')

# release: keep the binding, go zombie
R('''static void file_slot_release(file_slot *slot)
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
}''',
'''static void file_slot_release(file_slot *slot)
{
    /* Keep the token -> slot binding and the slot itself: the guest frees a
     * stream and keeps calling fwrite/fseek on the old value afterwards (its
     * logger does this every batch). On MSVC the freed _iob slot absorbs that
     * silently, and a reopen of the same file hands the SAME FILE* back. */
    if (slot->token >= FILE_TOKEN_BASE && slot->token < FILE_TOKEN_BASE + FILE_TOKEN_SPAN) {
        file_history_note(slot->token, slot->path, 'C');
    }
    if (slot->fp != NULL) {
        fclose(slot->fp);
    }
    slot->fp = NULL;
    slot->zombie = 1U;
}''')

# alloc: reuse zombies first (their token stays), else fresh slot+token
R('''    for (i = 0U; i < FILE_SLOT_COUNT; ++i) {
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
        slot->fp = fp;''',
'''    for (i = 0U; i < FILE_SLOT_COUNT; ++i) {
        if (g_slots[i].used && g_slots[i].zombie) {
            break;      /* reopen: same slot, same token, fresh stream */
        }
    }
    if (i == FILE_SLOT_COUNT) {
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
        g_tok_slot[t] = (signed short)i;
        g_tok_scan = (t + 1U) % FILE_TOKEN_SPAN;
    }
    {
        file_slot *slot = &g_slots[i];
        slot->zombie = 0U;
        if (slot->token == 0U) {
            slot->token = FILE_TOKEN_BASE + t;
        }
        slot->fp = fp;''')

R('''        slot->used = 1U;
        g_tok_slot[t] = (signed short)i;
        g_tok_scan = (t + 1U) % FILE_TOKEN_SPAN;
        *token_out = slot->token;''',
'''        slot->used = 1U;
        *token_out = slot->token;''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('zombie-slot semantics installed')
