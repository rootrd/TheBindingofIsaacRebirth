import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

# find the fwrite rejection site and append a history dump (first 3 times)
anchor = '''        FILE_ERR("fwrite: token 0x%{public}08x is unknown, already closed or not a stream issued by this module -> 0",
                 token);'''.replace('\n', nl)
assert s.count(anchor) == 1, s.count(anchor)
dump = ('''        {
            static unsigned shown = 0;
            if (shown < 3U) {
                unsigned n;
                shown++;
                for (n = 0U; n < FILE_HISTORY; ++n) {
                    unsigned k = (g_file_history_pos + FILE_HISTORY - 1U - n) % FILE_HISTORY;
                    if (g_file_history[k].token != 0U) {
                        FILE_ERR("  %c 0x%{public}08x %{public}s",
                                 g_file_history[k].kind, g_file_history[k].token,
                                 g_file_history[k].path);
                    }
                }
            }
        }''').replace('\n', nl)
s = s.replace(anchor, anchor + nl + dump, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('fwrite rejection history added')
