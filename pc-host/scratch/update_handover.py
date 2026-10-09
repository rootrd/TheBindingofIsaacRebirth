import io

p = r'F:\TheBindingofIsaacRebirth\交接-新会话入口.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
start = s.index('## ★★ 〇、当前状态与下一会话第一步')
end = s.index('---', start)

new = """## ★★ 〇、当前状态与下一会话第一步（2026-09-20 深夜收官）

### 一句话

**文件句柄层已彻底修好**（全速读归档：0 句柄错误、0 崩溃、guest 日志恢复），
游戏推进到资源加载阶段（版本横幅→归档挂载→逐条目加载）；
**唯一剩余阻塞 = 归档条目内容的解码**（全部条目 checksum 失败）。

### 已彻底解决（本轮终版，勿重做）

1. **FILE 令牌 = MSVC _iob 复用语义**（host_ext_file.c 终版设计；中间试错的三版全废弃）：
   - token = FILE_TOKEN_BASE(0x50000) + t，查表 g_tok_slot[t] -> 槽位；
   - fclose 后槽位变 zombie（绑定保留、fp=NULL）：游戏关流后仍用旧值 → 写=假成功、读=0、seek=0（与 MSVC 空 _iob 一致）；fopen 优先复用 zombie（同文件重开拿回同一个 token）；
   - file_standard_stream / file_standard_name 已恢复（曾被重构误删）；
   - 宿主 fclose 只在 file_slot_release() 里做一次（曾双重 fclose → glibc abort）。
   - 实测：unknown, already closed 从 1787 → 0；free() 崩溃消失。
2. **路径解析钩子补 8 字节栈**（guest_0166.c sub_00563120 钩子 c->esp += 8U）——mods 单例 NULL 虚调用 (0x3a93ba) 根因，已修。
3. **ISAAC 解码原生钩子已就位但从未命中**（host_ohos.c isaac_archive_xor_try()，钩在 guest_0175.c sub_005b06f0 入口；refresh 调翻译版 sub_005c2fd0）。没火是正确的——见卡点。

### 唯一卡点（范围已收窄到一个分支）

解码门控在 ArchivedFile::refill_buffer（guest_0172.c，0x59c39e..0x59c3c7）：
mode = decoder[0x10]；若 mode==0 则 mode = (cl==0 && esi==0x400)；mode!=0 才 call sub_005b06f0，否则密文直送 tinfl。
实测（[ARC] fread 追踪 + [DBG mode] 探针，脚本都在 pc-host/scratch/）：
- 归档读取完全正确（头部 7+1+4+2、index 20B/条、条目整读，从未出现 0x400 大小的读）；
- esi（读取长度）永远 < 0x400（474、152、161...）→ 门控永不满足 → sub_005b06f0 从未被调（钩子零打印）→ 密文直送 tinfl → MiniZ overflow → checksum 全挂；
- 校验器解压缓冲为全 0；直接 inflate animations.a 唯一条目（474B@offset18，前导 u32=474）失败——数据确实不是明文 deflate；
- 虚读 0x59c37e（call [eax+0x14]）在失败路径上也未被调用。

矛盾点（=钥匙）：同一份代码+归档在真 PC 能解开，说明"整流一次读 474B"的场景真机也走了某条解码路径——要么漏了 refill_buffer 更早的分支（sub_0059c170 = ArchivedFile::open 的解码器/ISAAC 状态初始化没成功，门控输入因此不对——上游注释原话：对象、解码器、ISAAC 状态分配都在 0x59c170），要么 [ebp-0x88]/[ebp-0x94] 的操作数语义被误读。

### 下一会话第一步（按序）

1. 重装探针（scratch/patch_buf_dbg.py、patch_mode_probe.py），复跑确认现象；
2. 对 sub_0059c170（ArchivedFile::open）逐段 verify_func.py + 插桩，查解码器对象分配
   与 ISAAC 状态种子为何没建立（重点：分配走的 bump heap、以及 open 时对 header mode
   字节的处理——animations.a 头 mode=1、graphics=0、多数=2，注意别按"mode=2 明文"想当然）;
3. 仍不通就做真 PC 对照：Windows 直跑正版 exe + API 钩子记录 refill_buffer 的
   fread/fseek 序列，与我们逐条 diff。
4. 修好判据不变：checksum 失败归零 → 19 shader → Viewport: 960x540 → 标题画面。

### 本轮其余修复与工具 —— 都不许重做

- MSVCP140 垫片 ABI：scratch/gen_msvcp_abi.py 从 SysWOW64 真 DLL 反汇编 ret N 生成
  isaac/msvcp_ret_table.h；guest_host_import() 调完垫片把 esp 钉到真值。
  （mangled 名不可信：basic_iostream 构造真 DLL 是 ret 8。）
- MSVC iostream 实装：sgetc/sbumpc/snextc 按 _IGnext=this+0x1c→this+0x14、
  _IGcount=this+0x2c→this+0x24、underflow=vtable+0x18、uflow=vtable+0x1c；
  setstate 状态字在 this+0xc；basic_ios/basic_streambuf 构造按真 DLL 重写
  （绝不能 memset 整个对象；streambuf 构造要建自指 _IG 指针，否则 setg 写地址 0）。
- 补翻 3 函数：sub_002599c0 / sub_002c8700 / sub_00481280（guest_extra_*.c，CMake 已登记，桩已删）。
- 工具：scratch/verify_func.py（翻译逐条比对，已证 sub_0056c7e0/4b3340/3a91c0/563880/5aeb00 全保真）、
  ISAAC_TRACE_ABI=1（esp 收支+callee-saved 违约点名）、dll_dis.py / msvcp_ret.py。
- WSL 已迁至 E:\\WSL\\Ubuntu2404（注册表已修正）；C 盘已清理（元凶是 22.5GB 的
  wsl-crashes 转储；以后宿主 abort 会再生成，记得清 %LOCALAPPDATA%\\Temp\\wsl-crashes）。
- 教训：gdb 在 -O1 宿主代码中间读 g_cpu.* 是寄存器缓存的陈旧值——只在函数边界读，或临时 fprintf。
"""

s = s[:start] + new.replace('\n', nl) + s[end:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('handover updated, bytes:', len(s))
