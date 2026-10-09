#!/usr/bin/env python3
"""Swap Index.ets's touch-control block for the Dead Cells style pad
(two sticks + free action buttons, drag & persist), keeping the launcher and
unpack logic untouched."""
import io

PAGE = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/ets/pages/Index.ets'
PAD = '/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/isaac_pad_final.ets'

src = io.open(PAGE, encoding='utf-8').read()
pad = io.open(PAD, encoding='utf-8').read()
pad = pad.replace('  @ObjectLink cfg: PadConfig;\n  @ObjectLink btn: FreeBtn;',
                  '  @ObjectLink btn: FreeBtn;\n  cfg: PadConfig = DEFAULT_STICK;')

MARK = '/* ===='
start = src.index(MARK)
end = src.index('@Entry')
head = ("import native from 'libentry.so';\n"
        "import { common } from '@kit.AbilityKit';\n"
        "import { display } from '@kit.ArkUI';\n"
        "import { fileIo } from '@kit.CoreFileKit';\n"
        "import { resourceManager } from '@kit.LocalizationKit';\n"
        "import { preferences } from '@kit.ArkData';\n\n")
src = head + pad + '\n' + src[end:]

start = src.index('  /* ---- free pads (Dead Cells style) ---- */')
end = src.index('  private startRender(): void {')
state = """  /* ---- virtual pad (Dead Cells style) ---- */
  @State pads: FreeBtn[] = defaultFreeButtons();
  @State stickMove: PadConfig = DEFAULT_STICK;
  @State stickShoot: PadConfig = DEFAULT_STICK;
  @State editMode: boolean = false;
  @State padReady: boolean = false;
  private stickMoveBtn: FreeBtn = new FreeBtn(900, 0, 'MOVE', 0, 0, 0);
  private stickShootBtn: FreeBtn = new FreeBtn(901, 0, 'SHOOT', 0, 0, 0);

  private loadPadLayout(): void {
    const ctx = getContext(this) as common.UIAbilityContext;
    loadFreeLayout(ctx).then((recs: FreeBtnRec[] | null): void => {
      if (recs !== null && recs.length > 0) {
        const arr: FreeBtn[] = [];
        for (let i = 0; i < recs.length; i++) {
          const r = recs[i];
          arr.push(new FreeBtn(i + 1, r.code, r.label, r.xRatio, r.yRatio, r.size));
        }
        this.pads = arr;
      }
      this.padReady = true;
    });
  }

"""
src = src[:start] + state + src[end:]

start = src.index('      /* Touch pads (Dead Cells style)')
end = src.index('      /* Debug overlay: only meaningful once the guest runs. */')
overlay = """      /* Virtual pad (Dead Cells style): two sticks and the action buttons,
       * only while the game runs. */
      if (this.launched && this.padReady) {
        StickPad({ btn: this.stickMoveBtn, cfg: this.stickMove, keys: STICK_KEYS_MOVE,
                   screenW: px2vp(this.surfW), screenH: px2vp(this.surfH) })
        StickPad({ btn: this.stickShootBtn, cfg: this.stickShoot, keys: STICK_KEYS_SHOOT,
                   screenW: px2vp(this.surfW), screenH: px2vp(this.surfH) })
        ForEach(this.pads, (item: FreeBtn) => {
          FreeButton({ btn: item, screenW: px2vp(this.surfW), screenH: px2vp(this.surfH) })
        }, (item: FreeBtn) => item.id.toString())
      }

"""
src = src[:start] + overlay + src[end:]
src = src.replace('      this.loadPadsFromPrefs();', '      this.loadPadLayout();', 1)

io.open(PAGE, 'w', encoding='utf-8').write(src)
print('pad swapped, lines =', len(src.split('\n')))
