import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

# Locate file_slot_alloc's body and replace it wholesale.
start_marker = 'static int file_slot_alloc(FILE *fp, const char *host_path, int is_standard,'
end_marker = 'static void file_slot_release(file_slot *slot)'
a = s.index(start_marker)
b = s.index(end_marker)

body = (
    'static int file_slot_alloc(FILE *fp, const char *host_path, int is_standard,' + nl +
    '                           uint32_t *token_out)' + nl +
    '{' + nl +
    '    unsigned i;' + nl +
    nl +
    '    for (i = 0U; i < FILE_SLOT_COUNT; ++i) {' + nl +
    '        if (!g_slots[i].used) {' + nl +
    '            file_slot *slot = &g_slots[i];' + nl +
    '            unsigned tries;' + nl +
    '            unsigned id = 0U;' + nl +
    '            slot->generation += 1U;' + nl +
    '            slot->fp = fp;' + nl +
    '            slot->is_standard = (uint8_t)(is_standard != 0);' + nl +
    '            slot->read_seen = 0U;' + nl +
    '            slot->write_seen = 0U;' + nl +
    '            slot->path[0] = 0;' + nl +
    '            if (host_path != NULL) {' + nl +
    '                snprintf(slot->path, sizeof slot->path, "%s", host_path);' + nl +
    '            }' + nl +
    '            for (tries = 0U; tries < FILE_TOKEN_ID_COUNT; ++tries) {' + nl +
    '                unsigned cand = (g_id_scan + tries) % FILE_TOKEN_ID_COUNT;' + nl +
    '                if (cand != 0U && g_id_slot[cand] < 0) {' + nl +
    '                    id = cand;' + nl +
    '                    break;' + nl +
    '                }' + nl +
    '            }' + nl +
    '            if (id == 0U) {' + nl +
    '                slot->used = 0U;' + nl +
    '                FILE_ERR("fopen: no free stream ids; \\"%{public}s\\" not opened",' + nl +
    '                         host_path != NULL ? host_path : "?");' + nl +
    '                return -1;' + nl +
    '            }' + nl +
    '            g_id_slot[id] = (signed short)i;' + nl +
    '            slot->token_id = id;' + nl +
    '            g_id_scan = (id + 1U) % FILE_TOKEN_ID_COUNT;' + nl +
    '            *token_out = FILE_TOKEN_BASE | ((uint32_t)id << FILE_TOKEN_ID_SHIFT);' + nl +
    '            file_history_note(*token_out, host_path, \'O\');' + nl +
    '            return 0;' + nl +
    '        }' + nl +
    '    }' + nl +
    '    FILE_ERR("fopen: all %u streams are in use; \\"%{public}s\\" not opened",' + nl +
    '             FILE_SLOT_COUNT, host_path != NULL ? host_path : "?");' + nl +
    '    return -1;' + nl +
    '}' + nl +
    nl
)
s = s[:a] + body + s[b:]

# release must hand the id back to the pool
rel_old = ('static void file_slot_release(file_slot *slot)' + nl +
           '{' + nl)
rel_new = ('static void file_slot_release(file_slot *slot)' + nl +
           '{' + nl +
           '    if (slot->token_id != 0U) {' + nl +
           '        g_id_slot[slot->token_id] = -1;' + nl +
           '        slot->token_id = 0U;' + nl +
           '    }' + nl)
if s.count(rel_old) == 1:
    s = s.replace(rel_old, rel_new, 1)
elif 'g_id_slot[slot->token_id] = -1;' not in s:
    raise SystemExit('release anchor missing')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('file_slot_alloc rewritten with id pool')
