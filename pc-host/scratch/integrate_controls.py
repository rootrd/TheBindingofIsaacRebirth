#!/usr/bin/env python3
"""Swap Index.ets's placeholder touch overlay for the Dead Cells style free
pad system (isaac_controls.ets), keeping the launcher/unpack logic intact."""
import io
import re

PAGE = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/ets/pages/Index.ets'
CTRL = '/mnt/f/TheBindingofIsaacRebirth/pc-host/scratch/isaac_controls.ets'

src = io.open(PAGE, encoding='utf-8').read()
ctrl = io.open(CTRL, encoding='utf-8').read()

# --- 1. header: drop the old constants + TouchStick class -------------------
start = src.index('/* Touch overlay, modelled on the Xbox pad')
end = src.index('@Entry\n@Component\nstruct Index {')
header = """import native from 'libentry.so';
import { common } from '@kit.AbilityKit';
import { display } from '@kit.ArkUI';
import { fileIo } from '@kit.CoreFileKit';
import { resourceManager } from '@kit.LocalizationKit';
import { preferences } from '@kit.ArkData';

"""
src = header + ctrl + '\n' + src[end:]

# --- 2. drop the old stick state + helpers + PadButton builder --------------
start = src.index('  /* ---- virtual gamepad state ---- */')
end = src.index('  private startRender(): void {')
src = src[:start] + src[end:]

# --- 3. drop the old overlay in build() ------------------------------------
start = src.index('      /* Virtual gamepad: only while the game runs')
end = src.index('      /* Debug overlay: only meaningful once the guest runs. */')
overlay = """      /* Touch pads (Dead Cells style): only while the game runs, and they
       * must not swallow taps outside the pads themselves. */
      if (this.launched && this.padsReady) {
        ForEach(this.pads, (item: FreeBtn) => {
          PadView({ btn: item, availX: 0, availY: 0, availW: this.surfW, availH: this.surfH })
        }, (item: FreeBtn) => item.id.toString())
      }

"""
src = src[:start] + overlay + src[end:]

# --- 4. drop the old onTouch stick call ------------------------------------
old_touch = """          native.touch(t.x, t.y, act, 0);
          /* Drive the virtual sticks (Xbox layout: left half move, right
           * half shoot). Only once the game is running. */
          if (this.launched) {
            this.handleSticks(e);
          }
"""
new_touch = """          native.touch(t.x, t.y, act, 0);
"""
if old_touch in src:
    src = src.replace(old_touch, new_touch)

# --- 5. add pad state + loader inside the component ------------------------
anchor = '  private startRender(): void {'
state = """  /* ---- free pads (Dead Cells style) ---- */
  @State pads: FreeBtn[] = defaultPads();
  @State padsReady: boolean = false;

  private loadPadsFromPrefs(): void {
    const ctx = getContext(this) as common.UIAbilityContext;
    loadPads(ctx).then((recs: PadRec[] | null): void => {
      if (recs !== null && recs.length > 0) {
        const arr: FreeBtn[] = [];
        for (let i = 0; i < recs.length; i++) {
          const r = recs[i];
          arr.push(new FreeBtn(i + 1, r.code, r.label, r.kind, r.xRatio, r.yRatio, r.size));
        }
        this.pads = arr;
      }
      this.padsReady = true;
    });
  }

"""
src = src.replace(anchor, state + anchor, 1)

# --- 6. call the loader when the game starts ------------------------------
src = src.replace("""      this.status = 'starting guest...';
      this.launched = true;
      this.startRender();""",
"""      this.status = 'starting guest...';
      this.loadPadsFromPrefs();
      this.launched = true;
      this.startRender();""", 1)

io.open(PAGE, 'w', encoding='utf-8').write(src)
print('integrated, lines =', len(src.split('\n')))
