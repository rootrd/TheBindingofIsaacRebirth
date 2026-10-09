import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

old = '''static file_slot *file_slot_get(uint32_t token, unsigned *index_out)
{
    uint32_t rel;
    unsigned id;
    signed short slot;

    if (token < FILE_TOKEN_BASE) {
        return NULL;
    }
    rel = token - FILE_TOKEN_BASE;
    if (rel >= FILE_TOKEN_SPAN) {
        return NULL;
    }
    id = (unsigned)(rel >> 6);
    if (id >= FILE_TOKEN_ID_COUNT) {
        return NULL;
    }
    slot = g_id_slot[id];
    if (slot < 0 || !g_slots[slot].used || g_slots[slot].token_id != id) {
        return NULL;
    }'''.replace('\n', nl)

new = '''static file_slot *file_slot_get(uint32_t token, unsigned *index_out)
{
    uint32_t rel;
    unsigned id;
    signed short slot;

    if (token < FILE_TOKEN_BASE) {
        FILE_ERR(\"get: below base 0x%{public}08x\", token);
        return NULL;
    }
    rel = token - FILE_TOKEN_BASE;
    if (rel >= FILE_TOKEN_SPAN) {
        FILE_ERR(\"get: 0x%{public}08x outside span\", token);
        return NULL;
    }
    id = (unsigned)(rel >> 6);
    if (id >= FILE_TOKEN_ID_COUNT) {
        FILE_ERR(\"get: 0x%{public}08x id %u too big\", token, id);
        return NULL;
    }
    slot = g_id_slot[id];
    if (slot < 0) {
        FILE_ERR(\"get: 0x%{public}08x id %u unbound\", token, id);
        return NULL;
    }
    if (!g_slots[slot].used) {
        FILE_ERR(\"get: 0x%{public}08x id %u slot %d dead\", token, id, slot);
        return NULL;
    }
    if (g_slots[slot].token_id != id) {
        FILE_ERR(\"get: 0x%{public}08x id %u but slot %d has %u\", token, id,
                 slot, g_slots[slot].token_id);
        return NULL;
    }'''.replace('\n', nl)

assert s.count(old) == 1, s.count(old)
io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(old, new, 1))
print('reason probes added')
