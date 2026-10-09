#!/usr/bin/env python3
import io

p = '/mnt/f/TheBindingofIsaacRebirth/交接-新会话入口.md'
lines = io.open(p, encoding='utf-8').read().split('\n')

add = (
    '   **★★★最新根因（2026-09-19 终版，会话收官）**：SEH 分发已遍历到 frame=0、调用 handler '
    '0x305f2420，但 handler 直接 TerminateProcess（fastfail）；修复 Context.Eip（原为 0，handler '
    '无法展开帧）后，gdb 抓到**真正的崩溃点**：\n'
    '   ```\n'
    '   memcpy(__len=16, __dest=0x7ffff7bffff8)   ← arena 末尾外\n'
    '   stx(a=0xFFFFFFF8)                          ← 目标地址 = -8！\n'
    '   sub_000226f0 (guest_0004.c:16221)\n'
    '   ← sub_0025ceb0 (guest_0061.c:18446)\n'
    '   ← sub_002597b0 (guest_extra_002597b0.c:141)  ← 我们补翻的函数\n'
    '   ← sub_0025bbc0 (PersistentGameData::Load)\n'
    '   ```\n'
    '   **guest 在异常路径上往地址 0xFFFFFFF8（-8）写 16 字节 SSE**——这是一个空指针减 8 的经典 '
    '模式，说明某个对象/缓冲为 NULL，或补翻的 sub_002597b0 参数/时序与原生有差异（它是 throw '
    'helper，本该抛出异常，现在却走进了正常路径并 deref 空指针）。**下一会话第一件事**：读 '
    'guest_0004.c:16221（sub_000226f0）与 guest_0061.c:18446（sub_0025ceb0）的翻译，确认那个 '
    '0xFFFFFFF8 从哪来（通常是 `[ebp-8]` 被当成绝对地址，或对象指针为 0 后减 8）；再对照 '
    'sub_002597b0 的原生语义（它应由 _CxxThrowException 抛出而不是继续执行）。'
)
lines.insert(10, add)
io.open(p, 'w', encoding='utf-8').write('\n'.join(lines))
print('written')
