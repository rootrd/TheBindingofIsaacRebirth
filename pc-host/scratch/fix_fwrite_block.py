import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

broken_head = '    if (fp == NULL) {' + nl + \
    '        FILE_ERR("fwrite: token 0x%{public}08x is unknown, already closed or "'
start = s.index(broken_head)
end = s.index('    if (!file_guest_range(ptr, total)) {', start)
fixed = (
    '    if (fp == NULL) {' + nl +
    '        if (slot != NULL) {' + nl +
    '            /* Zombie stream: MSVC\'s freed _iob absorbs the write. */' + nl +
    '            errno = entry_errno;' + nl +
    '            c->eax = count;' + nl +
    '            file_cdecl_ret(c);' + nl +
    '            return;' + nl +
    '        }' + nl +
    '        FILE_ERR("fwrite: token 0x%{public}08x is unknown, already closed or "' + nl +
    '                 "not a stream issued by this module -> 0", token);' + nl +
    '        file_errno_set(EBADF);' + nl +
    '        errno = entry_errno;' + nl +
    '        c->eax = 0U;' + nl +
    '        file_cdecl_ret(c);' + nl +
    '        return;' + nl +
    '    }' + nl
)
s = s[:start] + fixed + s[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('fwrite block rewritten')
