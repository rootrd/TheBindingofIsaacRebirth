import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

def R(old, new, n=1):
    global s
    o, w = old.replace('\n', nl), new.replace('\n', nl)
    assert s.count(o) == n, (s.count(o), old[:60])
    s = s.replace(o, w, n)

# 1) release: collapse the duplicated id-return block, keep one history note
R('''    if (slot->token_id != 0U) {
        g_id_slot[slot->token_id] = -1;
        slot->token_id = 0U;
    }
    if (slot->token_id != 0U) {
        g_id_slot[slot->token_id] = -1;
        slot->token_id = 0U;
    }
    file_history_note(file_token_make((unsigned)(slot - g_slots), slot->generation), slot->path, 'C');
    slot->fp = NULL;''',
'''    if (slot->token_id != 0U) {
        file_history_note(FILE_TOKEN_BASE |
                              ((uint32_t)slot->token_id << FILE_TOKEN_ID_SHIFT),
                          slot->path, 'C');
        g_id_slot[slot->token_id] = -1;
        slot->token_id = 0U;
    }
    slot->fp = NULL;''')

# 2) _get_osfhandle: mint the token the same way alloc does
R('''            c->eax = file_token_make(index, g_slots[index].generation);''',
'''            c->eax = FILE_TOKEN_BASE |
                     ((uint32_t)g_slots[index].token_id << FILE_TOKEN_ID_SHIFT);''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('fixed release + osfhandle')
