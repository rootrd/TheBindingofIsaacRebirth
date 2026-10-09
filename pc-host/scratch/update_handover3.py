import io

p = r'F:\TheBindingofIsaacRebirth\交接-新会话入口.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'

old = '### 唯一卡点（2026-09-20 深夜最新进展——比上一版交接前进了一大步）' + nl
assert s.count(old) == 1
new = """### ★★★ 归档格式已完全破解（2026-09-20 第二次深夜更新，必读）★★★

**`*.a` 条目布局（Python 全流程验证过，afterbirth.a 41 块解出 41202B 合法 XML）**：
```
条目数据 = 一串 [u32 chunk_len][chunk_bytes]
  - chunk_len 的最高位 = 末块标志（如 0x8000024A → 长度 0x24A 的末块）
  - len == 0x400 的块 = ISAAC 加密块（refill 里 memcpy 到输出后调 sub_005b06f0 XOR 解码）
  - len != 0x400 的块 = 明文 deflate 块，直接送 tinfl（HAS_MORE_INPUT 流式，跨块连续解压）
  - 游戏的 refill 每次读一个 chunk；mode 字节缓存后不再重算
**校验和**：fold(解压后全部明文)，初值 0xababeb98（在 sub_00563880 里
  `mov esi, 0xababeb98`，memset 不破坏 esi），算法 s = (s<<31) + w + (s>>1) 逐 u32。
  Python 验证：afterbirth 前 50 条里 13 条纯 deflate 条目全部精确匹配！
  其余 34 条 = 含 0x400 加密块的条目（明文需先解 ISAAC），不是公式错。
**ISAAC 种子** = 条目名字哈希的第二个 u32（[DBG open] 实测：afterbirth entry0
  名字 d848f42a-e92c9f03 → 种子 0xe92c9f03）。
**ISAAC 初始化（0x5c2f10）不是 Jenkins randinit！** 用的是 **PCG32 流**
  （乘数 0x4C957F2D5851F42D，XSH-RR 输出）生成 256 个 dword 填入状态，
  再调 0x9c30b0 做初始混合，最后 state[0]=0。刷新函数 0x5c2fd0 = 标准 Jenkins isaac()
  （c++/b+=c/移位 13,6,2,16/mm[(i+128)]/r[]）已确认。
**完整反汇编已固化：pc-host/scratch/isaac_init_disasm.txt（init 120 条 + 0x9c30b0 + refresh）。
  活状态样本：isaac_state.bin（种子 0xe92c9f03 的 post-init 状态，可对照 Python 转写）。**

**下一会话第一步（纯机械工作，无未知数）**：
1. 把 isaac_init_disasm.txt 的 0x5c2f10 转写成 Python（PCG32 + 状态填充），
   用 isaac_state.bin 对照验证（种子 0xe92c9f03）；
2. 全流程 Python 验证：解码一个含 0x400 块的条目（如 afterbirth e13，种子 0x2de715be），
   fold(init=0xababeb98) == 索引 checksum；
3. 在宿主实现解码：最稳的做法是给 `sub_005b06f0` 的现有钩子
   （host_ohos.c isaac_archive_xor_try）换上 PCG init——或者干脆在 `h_file_fread`
   之后、tinfl 之前由 refill 的翻译代码自然走通（游戏自己的 sub_005b06f0 翻译已
   保真，理论上只要 init 正确即可全部自洽）；
4. 判据不变：checksum 归零 → 19 shader → Viewport: 960x540 → 标题画面。

---
### 前一版卡点记录（已被上面的破解覆盖，留档）
""" + nl
s = s.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('handover updated with the cracked format')
