import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\host_ext_file.c'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

if 'static FILE *file_standard_stream' in s:
    print('already present')
else:
    anchor = '/* token = FILE_TOKEN_BASE + t. t values are handed out lowest-free-first'
    assert s.count(anchor) == 1, s.count(anchor)
    helpers = '''/* The three standard streams keep their MSVC-style identity values (the IOB
 * base); closing them is refused and writes are redirected to the host's own
 * streams so the harness log survives. */
static FILE *file_standard_stream(uint32_t token, unsigned *std_index)
{
    if (token >= FILE_IOB_BASE && token < FILE_IOB_BASE + FILE_IOB_COUNT) {
        if (std_index != NULL) {
            *std_index = (unsigned)(token - FILE_IOB_BASE);
        }
        switch (token - FILE_IOB_BASE) {
        case 0U: return stdin;
        case 1U: return stdout;
        default: return stderr;
        }
    }
    return NULL;
}

static const char *file_standard_name(unsigned std_index)
{
    static const char *const names[FILE_IOB_COUNT] = { "stdin", "stdout", "stderr" };
    return (std_index < FILE_IOB_COUNT) ? names[std_index] : "?";
}

'''.replace('\n', nl)
    s = s.replace(anchor, helpers + anchor, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
    print('standard-stream helpers restored')
