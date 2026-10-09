#!/usr/bin/env python3
"""Match the Dead Cells default layout exactly: the two top-corner keys at
(0.01,0.01) and (0.92,0.01) at size 46, and the right-hand skill keys on the
same ratios/sizes ohosdc uses -- only the labels change to Isaac's actions.
The right stick (which ohosdc has no equivalent of, being a single-stick
game) goes bottom-right at a smaller diameter so it cannot overlap them."""
import io

PAGE = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/ets/pages/Index.ets'
src = io.open(PAGE, encoding='utf-8').read()

old_start = src.index('function defaultFreeButtons(): FreeBtn[] {')
old_end = src.index('}', src.index('return list;', old_start)) + 1
new_fn = """function defaultFreeButtons(): FreeBtn[] {
  const list: FreeBtn[] = [];
  /* Top row -- same ratios and sizes as the Dead Cells port's 菜单/地图 keys. */
  list.push(new FreeBtn(1, 0x1B, 'PAUSE', 0.01, 0.01, 46));
  list.push(new FreeBtn(2, 0x09, 'MAP', 0.92, 0.01, 46));
  /* Right-hand skill cluster -- the port's 主/副/跳/滚 positions verbatim. */
  list.push(new FreeBtn(3, 0x45, 'BOMB', 0.72, 0.55, 60));
  list.push(new FreeBtn(4, 0x51, 'CARD', 0.82, 0.40, 60));
  list.push(new FreeBtn(5, 0x20, 'ITEM', 0.80, 0.70, 60));
  list.push(new FreeBtn(6, 0x11, 'SWAP', 0.90, 0.55, 60));
  return list;
}"""
src = src[:old_start] + new_fn + src[old_end:]

# Right stick: bottom-right, small diameter, clear of the skill cluster.
src = src.replace("const DEFAULT_STICK_RIGHT: PadConfig = { xRatio: 0.74, yRatio: 0.45, diameter: 200, btnSize: 64 };",
                  "const DEFAULT_STICK_RIGHT: PadConfig = { xRatio: 0.84, yRatio: 0.72, diameter: 150, btnSize: 52 };", 1)

io.open(PAGE, 'w', encoding='utf-8').write(src)
print('layout matched to ohosdc')
