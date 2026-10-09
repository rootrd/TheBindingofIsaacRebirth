#!/usr/bin/env python3
import io

p = '/mnt/f/TheBindingofIsaacRebirth/交接-新会话入口.md'
lines = io.open(p, encoding='utf-8').read().split('\n')

add = (
    '   **★★★真机与PC同步到最终卡点（2026-09-19 收官）**：真机 run 到 '
    '**19 个 shader 全部初始化成功 → PersistentGameData → TerminateProcess(SEH fastfail)**，'
    'PC 完全相同。**触摸控制已完全照搬死亡细胞**（VirtualPad 8扇区掩码 + FreeButton + '
    'Tap/Pan 拖动 + preferences 持久化；布局与死亡细胞一致：PAUSE/MAP 顶部两角 46、'
    'BOMB/CARD/ITEM/SWAP 右侧 60 散布、MOVE 摇杆左下 200、SHOOT 摇杆右下 150）——'
    '**真机截图确认布局正确，FPS 119 渲染运行**。**唯一剩余卡点 = SEH 异常展开的控制流转移**：'
    'h_cxx_throw 已接 isaac_seh_dispatch、已能遍历 FS:[0] 链并调用帧 handler（实测 '
    'handler=0x305f2420），但 handler 返回后需要按 EXCEPTION_DISPOSITION 语义决定：'
    '1=ContinueSearch 继续下一帧；0=ContinueExecution 表示已处理。MSVC 的 __except_handler4 '
    '通过 ESP 转移 + funclet 调用完成 catch，当前实现调用后继续遍历、缺少 RtlUnwind/funclet '
    '执行，导致未捕获 → TerminateProcess。**下一会话**：读 sub_005f22e5（__except_handler4，'
    'guest_0184.c:20164）翻译，确认它期望的帧布局与返回语义；实现调用 handler 后按返回值转移'
    '控制流（若 catch 则执行 funclet 并跳到 handler 指定的 EIP）。'
)
lines.insert(10, add)
io.open(p, 'w', encoding='utf-8').write('\n'.join(lines))
print('status written')
