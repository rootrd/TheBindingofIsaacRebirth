import io

p = r'F:\TheBindingofIsaacRebirth\交接-新会话入口.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

old = '### 唯一卡点（范围已收窄到一个分支）' + nl
assert s.count(old) == 1
new_section = """### 唯一卡点（2026-09-20 深夜最新进展——比上一版交接前进了一大步）

上一版说"sub_005b06f0 从未被调、0x59c170 从未被调"——**作废**！原因是探针把 guest_0172.c
写坏后编译静默失败、二进制是旧的。修复编译后（compile_extras_lf.sh 不查错，一定要看 gcc 输出），
探针给出了全新的、决定性的图景：

1. **ArchivedFile::open（sub_0059c170）正常运行**：mode=2 归档逐个初始化 ISAAC 状态
   （malloc 0x810 → call 0x5c2f10(state, seed)）。**种子 = 条目/归档的名字哈希**
   （实测 animations 条目 seed=b807817b，正是索引里名字第二个 u32）——种子确定、正确。
2. **refill_buffer（sub_0059c270）正常运行**，vtable+0x14 虚读 = 0x596560（纯 fread 包装，
   读大小 = 各条目压缩尺寸 0x139/0x115...，从无 0x400 读）。
3. **解码确实发生了**：refill 时对象输入缓冲（obj+0x1c）的内容 ≠ 归档文件原始字节
   ——说明有东西把读到的数据变换过了。
4. **但结果仍不对**：tinfl 报 overflow、校验器缓冲全 0、
   用活内存 dump 的真实 ISAAC 状态（r[0]=27e9df89, a=4d3ca9c7, b=7751098d, c=1）
   直接 XOR 输入缓冲也得不到 deflate/明文。

**现场证据已固化在 pc-host/scratch/：isaac_state.bin（0x810 真状态）、entry_in.bin（输入
缓冲 1024B）、dump_state2.sh（重取 dump）。**

**下一会话第一步（按序）**：
1. 用 dump_state2.sh 重取状态+输入（换 n==1/2/3 对不同归档），对 entry_in.bin 做
   「与 r[]/mm[] 各种 XOR 组合 + 上下文判断」直至还原出合法 deflate 或明文——
   上游 host_vita_archive_xor.c 就是这条路的成品（key=ld32(state+4+index*4)，字对齐
   XOR、每 256 字刷新一次），注意它的 count 是本次解码长度、index 是 state[0]。
2. 若 XOR 始终不出明文：怀疑"每个 0x400 块用独立的 keystream 段 + index 递进"
   的块序没对上——回 refill_buffer（guest_0172.c 0x59c270..0x59c671）逐行读翻译，
   把 [ebp-0x88]/[ebp-0x94]/[ebp-0x8c] 的赋值链理清（它们分别喂了 mode 门控和
   tinfl 参数）。
3. 修好判据不变：checksum 失败归零 → 19 shader → Viewport: 960x540 → 标题画面。

### 唯一卡点的历史排查记录（勿重走）

""" + nl
s = s.replace(old, new_section, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('handover updated with the latest findings')
