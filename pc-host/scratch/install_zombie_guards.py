import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

def R(old, new, n=1):
    global s
    o, w = old.replace('\n', nl), new.replace('\n', nl)
    assert s.count(o) == n, (s.count(o), old[:70])
    s = s.replace(o, w, n)

# fwrite: benign success on a zombie
R('''    errno = 0;
    put = fwrite(isaac_arena_base + ptr, (size_t)size, (size_t)count, fp);''',
'''    if (fp == NULL) {
        /* Zombie stream (closed, guest still holds the value): MSVC's freed
         * _iob absorbs the write; pretend success without touching memory. */
        errno = entry_errno;
        c->eax = count;
        file_cdecl_ret(c);
        return;
    }
    errno = 0;
    put = fwrite(isaac_arena_base + ptr, (size_t)size, (size_t)count, fp);''')

# fread: 0 items on a zombie
R('''    errno = 0;
    got = fread''')
if s.count('errno = 0;' + nl + '    got = fread') == 1:
    R('''    errno = 0;
    got = fread''',
      '''    if (fp == NULL) {
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    errno = 0;
    got = fread''')

# fseek: success on a zombie
R('''    errno = 0;
    rc = fseek''')
if s.count('errno = 0;' + nl + '    rc = fseek') == 1:
    R('''    errno = 0;
    rc = fseek''',
      '''    if (fp == NULL) {
        errno = entry_errno;
        c->eax = 0U;
        file_cdecl_ret(c);
        return;
    }
    errno = 0;
    rc = fseek''')

# fclose: skip the host fclose when fp is already gone (release handles state)
R('''    errno = 0;
    rc = fclose(slot->fp);''',
'''    errno = 0;
    rc = 0;
    if (slot->fp != NULL) {
        rc = fclose(slot->fp);
    }''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('zombie guards installed')
