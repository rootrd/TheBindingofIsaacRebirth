import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

# 1) remove the leftover mangled file_token_make function (from its signature
#    to the start of the history comment or the next function, whichever first)
start = s.find('static uint32_t file_token_make(')
if start != -1:
    nxt = s.find('/* Rolling history of stream open/close events.', start)
    if nxt == -1:
        nxt = s.find('static file_slot *file_slot_get', start)
    s = s[:start] + s[nxt:]

# 2) declare g_id_slot before every use
anchor = 'static file_slot g_slots[FILE_SLOT_COUNT];'
assert s.count(anchor) == 1
s = s.replace(anchor,
              anchor + nl +
              'static signed short g_id_slot[FILE_TOKEN_ID_COUNT];   /* id -> live slot, -1 free */',
              1)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('cleaned: token_make removed, g_id_slot declared')
