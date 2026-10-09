import io

p = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\guest_0172.c'
s = io.open(p, encoding='utf-8', newline='').read()

# The probe format strings contain a raw newline where \n should be; repair both.
bad1 = 'esi=%08x\n",'
good1 = 'esi=%08x\\n",'
print('bad1 count:', s.count(bad1))
s = s.replace(bad1, good1)

bad2 = 'in=%02x%02x%02x%02x%02x%02x%02x%02x\n",'
good2 = 'in=%02x%02x%02x%02x%02x%02x%02x%02x\\n",'
print('bad2 count:', s.count(bad2))
s = s.replace(bad2, good2)

# also the probe line before the function comment broke the file: it sits
# between functions and is fine as a statement? No - it is at file scope.
# Move it inside sub_0059c270 instead: find the file-scope probe and relocate.
fscope = '    if (getenv("ISAAC_DBG_BUFFER")) { static int rn_ = 0; if (rn_ < 6) { rn_++;\n'
idx = s.find(fscope)
if idx != -1:
    # check what precedes: if it is the comment line for sub_0059c270, we must
    # insert the probe AFTER the opening brace of the function.
    end = s.find('        unsigned obj_', idx)
    block_end = s.find('} }\n', end) + len('} }\n')
    block = s[idx:block_end]
    # remove from the wrong place
    s = s[:idx] + s[block_end:]
    # insert after sub_0059c270's opening brace
    anchor = 'void sub_0059c270(CPU *__restrict c)\n{\n'
    if s.count(anchor) != 1:
        anchor = anchor.replace('\n', '\r\n')
    assert s.count(anchor) == 1, s.count(anchor)
    s = s.replace(anchor, anchor + block, 1)
    print('refill probe relocated into the function')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('fixed')
