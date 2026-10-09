#!/usr/bin/env python3
"""Install the Dead Cells style pad (sticks + free buttons) into Index.ets."""
import io

PAGE = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/ets/pages/Index.ets'
PAD = '/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/isaac_pad_v2.ets'

src = io.open(PAGE, encoding='utf-8').read()
pad = io.open(PAD, encoding='utf-8').read()

start = src.index('/* ====')
end = src.index('@Entry')
head = ("import native from 'libentry.so';\n"
        "import { common } from '@kit.AbilityKit';\n"
        "import { display } from '@kit.ArkUI';\n"
        "import { fileIo } from '@kit.CoreFileKit';\n"
        "import { resourceManager } from '@kit.LocalizationKit';\n"
        "import { preferences } from '@kit.ArkData';\n\n")
src = head + pad + '\n' + src[end:]

# component state
start = src.index('  /* ---- virtual pad (Dead Cells style) ---- */')
end = src.index('  private startRender(): void {')
state = """  /* ---- virtual pad (Dead Cells style) ---- */
  @State pads: FreeBtn[] = defaultFreeButtons();
  @State stickLeft: PadConfig = DEFAULT_STICK_LEFT;
  @State stickRight: PadConfig = DEFAULT_STICK_RIGHT;
  @State editMode: boolean = false;
  @State padReady: boolean = false;
  @State selSide: string = '';
  @State selId: number = 0;

  private loadPadLayout(): void {
    const ctx = getContext(this) as common.UIAbilityContext;
    loadLayout(ctx).then((recs: FreeBtnRec[] | null): void => {
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

  private savePadLayout(): void {
    const ctx = getContext(this) as common.UIAbilityContext;
    const recs: FreeBtnRec[] = [];
    for (let i = 0; i < this.pads.length; i++) {
      const b = this.pads[i];
      recs.push({ code: b.code, label: b.label, xRatio: b.xRatio, yRatio: b.yRatio, size: b.size });
    }
    saveLayout(ctx, recs, { left: this.stickLeft, right: this.stickRight });
  }

  private selectSide(side: string): void {
    this.selSide = side;
    this.selId = 0;
    this.stickLeft = { xRatio: this.stickLeft.xRatio, yRatio: this.stickLeft.yRatio,
                       diameter: this.stickLeft.diameter, btnSize: this.stickLeft.btnSize };
  }

"""
src = src[:start] + state + src[end:]

# overlay
start = src.index('      /* Virtual pad (Dead Cells style)')
end = src.index('      /* Debug overlay: only meaningful once the guest runs. */')
overlay = """      /* Virtual pad (Dead Cells style): two stick discs plus the action
       * keys, only while the game runs. */
      if (this.launched && this.padReady) {
        VirtualPad({
          config: this.stickLeft, buttons: STICK_LEFT_BTNS,
          editable: this.editMode, selected: this.selSide === 'left',
          screenW: px2vp(this.surfW), screenH: px2vp(this.surfH),
          onBtnDown: (code: number) => { native.key(code, 1); },
          onBtnUp: (code: number) => { native.key(code, 0); },
          onSelect: () => { this.selectSide('left'); }
        })
        VirtualPad({
          config: this.stickRight, buttons: STICK_RIGHT_BTNS,
          editable: this.editMode, selected: this.selSide === 'right',
          screenW: px2vp(this.surfW), screenH: px2vp(this.surfH),
          onBtnDown: (code: number) => { native.key(code, 1); },
          onBtnUp: (code: number) => { native.key(code, 0); },
          onSelect: () => { this.selectSide('right'); }
        })
        ForEach(this.pads, (item: FreeBtn) => {
          FreeButton({
            btn: item, editable: this.editMode,
            screenW: px2vp(this.surfW), screenH: px2vp(this.surfH),
            onPressed: (code: number) => { native.key(code, 1); },
            onReleased: (code: number) => { native.key(code, 0); },
            onSelect: () => { this.selSide = ''; this.selId = item.id;
                              item.selected = true; }
          })
        }, (item: FreeBtn) => item.id.toString())
      }

"""
src = src[:start] + overlay + src[end:]
src = src.replace('      this.loadPadLayout();', '      this.loadPadLayout();', 1)

io.open(PAGE, 'w', encoding='utf-8').write(src)
print('pad v2 installed, lines =', len(src.split('\n')))
