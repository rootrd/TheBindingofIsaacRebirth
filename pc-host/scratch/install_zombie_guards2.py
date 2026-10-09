import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

def R(old, new, n=1):
    global s
    o, w = old.replace('\n', nl), new.replace('\n', nl)
    assert s.count(o) == n, (s.count(o), old[:70])
    s = s.replace(o, w, n)

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

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('fread/fseek guards in')
