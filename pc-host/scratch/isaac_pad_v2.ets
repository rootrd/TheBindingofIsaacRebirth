/* ============================================================================
 * Virtual pad — code structure copied from the Dead Cells port (ohosdc) with
 * only the key catalogue swapped to Isaac's Xbox layout:
 *   VirtualPad   stick disc with 4 direction buttons, 8-sector mask,
 *                tap-select + pan-drag in edit mode
 *   FreeButton   single action key, @ObjectLink so a drag re-renders alone
 *   persistence  preferences per button (position/size/label/code)
 *   MOVE stick  -> W A S D     SHOOT stick -> arrow keys
 *   BOMB=E  CARD=Q  ITEM=Space  SWAP=Ctrl  MAP=Tab  PAUSE=Esc
 * ========================================================================= */

const PREF_NAME = 'isaac_pad';
const PREF_KEY_FREE = 'free_buttons_v2';
const PREF_KEY_STICKS = 'sticks_v2';

type PadSlot = 'up' | 'down' | 'left' | 'right';

/** 单个圆盘的可调整布局：位置比例 + 直径 + 按钮尺寸 */
interface PadConfig {
  xRatio: number;
  yRatio: number;
  diameter: number;
  btnSize: number;
}

const DEFAULT_STICK_LEFT: PadConfig = { xRatio: 0.02, yRatio: 0.45, diameter: 200, btnSize: 64 };
const DEFAULT_STICK_RIGHT: PadConfig = { xRatio: 0.74, yRatio: 0.45, diameter: 200, btnSize: 64 };

/** 自由键记录：全量存 code+label+位置+大小 */
interface FreeBtnRec {
  code: number;
  label: string;
  xRatio: number;
  yRatio: number;
  size: number;
}

interface SticksRec {
  left: PadConfig;
  right: PadConfig;
}

/** 可映射键位目录：code 为 Windows VK，label 是按钮文字 */
interface KeyDef {
  code: number;
  label: string;
}
const KEY_CATALOG: KeyDef[] = [
  { code: 0x45, label: 'BOMB' },
  { code: 0x51, label: 'CARD' },
  { code: 0x20, label: 'ITEM' },
  { code: 0x11, label: 'SWAP' },
  { code: 0x09, label: 'MAP' },
  { code: 0x1B, label: 'PAUSE' },
];

/** 摇杆四方向 → VK（左盘 WASD，右盘方向键） */
const STICK_LEFT_BTNS: VBtn[] = [
  { slot: 'up', code: 0x57, symbol: 'W' },
  { slot: 'down', code: 0x53, symbol: 'S' },
  { slot: 'left', code: 0x41, symbol: 'A' },
  { slot: 'right', code: 0x44, symbol: 'D' },
];
const STICK_RIGHT_BTNS: VBtn[] = [
  { slot: 'up', code: 0x26, symbol: '↑' },
  { slot: 'down', code: 0x28, symbol: '↓' },
  { slot: 'left', code: 0x25, symbol: '←' },
  { slot: 'right', code: 0x27, symbol: '→' },
];

const PRESS_COLOR = '#e6ff6a1a';

function defaultFreeButtons(): FreeBtn[] {
  const list: FreeBtn[] = [];
  list.push(new FreeBtn(1, 0x45, 'BOMB', 0.70, 0.80, 62));
  list.push(new FreeBtn(2, 0x51, 'CARD', 0.80, 0.68, 62));
  list.push(new FreeBtn(3, 0x20, 'ITEM', 0.62, 0.68, 62));
  list.push(new FreeBtn(4, 0x11, 'SWAP', 0.88, 0.50, 56));
  list.push(new FreeBtn(5, 0x09, 'MAP', 0.97, 0.09, 54));
  list.push(new FreeBtn(6, 0x1B, 'PAUSE', 0.50, 0.06, 54));
  return list;
}

interface VBtn {
  slot: PadSlot;
  code: number;
  symbol: string;
}

@Observed
class FreeBtn {
  id: number;
  code: number;
  label: string;
  xRatio: number;
  yRatio: number;
  size: number;
  selected: boolean;
  edit: boolean;
  pressed: boolean;

  constructor(id: number, code: number, label: string,
              xRatio: number, yRatio: number, size: number) {
    this.id = id;
    this.code = code;
    this.label = label;
    this.xRatio = xRatio;
    this.yRatio = yRatio;
    this.size = size;
    this.selected = false;
    this.edit = false;
    this.pressed = false;
  }
}

async function saveLayout(context: common.UIAbilityContext,
                          recs: FreeBtnRec[], sticks: SticksRec): Promise<void> {
  try {
    const pref = await preferences.getPreferences(context, PREF_NAME);
    await pref.put(PREF_KEY_FREE, JSON.stringify(recs));
    await pref.put(PREF_KEY_STICKS, JSON.stringify(sticks));
    await pref.flush();
  } catch (e) {
  }
}

async function loadLayout(context: common.UIAbilityContext): Promise<FreeBtnRec[] | null> {
  try {
    const pref = await preferences.getPreferences(context, PREF_NAME);
    const raw = await pref.get(PREF_KEY_FREE, '') as string;
    if (!raw) {
      return null;
    }
    return JSON.parse(raw) as FreeBtnRec[];
  } catch (e) {
    return null;
  }
}

/** 摇杆圆盘（照搬 ohosdc VirtualPad）：四方向按钮 + 8 扇区掩码 + 拖动 */
@Component
struct VirtualPad {
  @Link config: PadConfig;
  buttons: VBtn[] = STICK_LEFT_BTNS;
  @Prop editable: boolean = false;
  @Prop selected: boolean = false;
  @Prop screenW: number = 0;
  @Prop screenH: number = 0;
  onBtnDown: (code: number) => void = () => {};
  onBtnUp: (code: number) => void = () => {};
  onSelect: () => void = () => {};
  private baseXRatio: number = 0;
  private baseYRatio: number = 0;
  @State private activeMask: number = 0;
  private touchId: number = -1;

  private slotBit(slot: PadSlot): number {
    if (slot === 'up') {
      return 1;
    }
    if (slot === 'down') {
      return 2;
    }
    if (slot === 'left') {
      return 4;
    }
    return 8;
  }

  private dirCode(bit: number): number {
    let slot: PadSlot = 'right';
    if (bit === 1) {
      slot = 'up';
    } else if (bit === 2) {
      slot = 'down';
    } else if (bit === 4) {
      slot = 'left';
    }
    for (let i = 0; i < this.buttons.length; i++) {
      if (this.buttons[i].slot === slot) {
        return this.buttons[i].code;
      }
    }
    return 0;
  }

  private setMask(newMask: number): void {
    const bits: number[] = [1, 2, 4, 8];
    for (let i = 0; i < bits.length; i++) {
      const bit = bits[i];
      const was = (this.activeMask & bit) !== 0;
      const now = (newMask & bit) !== 0;
      if (now && !was) {
        this.onBtnDown(this.dirCode(bit));
      } else if (!now && was) {
        this.onBtnUp(this.dirCode(bit));
      }
    }
    this.activeMask = newMask;
  }

  /** 触点相对圆心 → 方向掩码（8 扇区，圆心 deadzone 不激活） */
  private maskFromPoint(lx: number, ly: number): number {
    const c = this.config.diameter / 2;
    const dx = lx - c;
    const dy = ly - c;
    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist < this.config.diameter * 0.18) {
      return 0;
    }
    const ang = Math.atan2(dy, dx) * 180 / Math.PI;
    if (ang >= -22.5 && ang < 22.5) {
      return 8;
    }
    if (ang >= 22.5 && ang < 67.5) {
      return 8 | 2;
    }
    if (ang >= 67.5 && ang < 112.5) {
      return 2;
    }
    if (ang >= 112.5 && ang < 157.5) {
      return 4 | 2;
    }
    if (ang >= 157.5 || ang < -157.5) {
      return 4;
    }
    if (ang >= -157.5 && ang < -112.5) {
      return 4 | 1;
    }
    if (ang >= -112.5 && ang < -67.5) {
      return 1;
    }
    return 8 | 1;
  }

  private slotPos(slot: PadSlot): number[] {
    const d = this.config.diameter;
    const s = this.config.btnSize;
    const near = d - s;
    const mid = (d - s) / 2;
    if (slot === 'up') {
      return [mid, 0];
    }
    if (slot === 'left') {
      return [0, mid];
    }
    if (slot === 'right') {
      return [near, mid];
    }
    return [mid, near];
  }

  @Builder
  Btn(b: VBtn) {
    Text(b.symbol)
      .fontSize(this.config.btnSize * 0.4)
      .fontColor('#f5f5f5')
      .textAlign(TextAlign.Center)
      .width(this.config.btnSize)
      .height(this.config.btnSize)
      .borderRadius(this.config.btnSize / 2)
      .backgroundColor((this.activeMask & this.slotBit(b.slot)) !== 0 ? PRESS_COLOR : '#66202028')
      .position({ x: this.slotPos(b.slot)[0], y: this.slotPos(b.slot)[1] })
      .hitTestBehavior(HitTestMode.None)
  }

  build() {
    Stack() {
      Circle()
        .width(this.config.diameter - this.config.btnSize)
        .height(this.config.diameter - this.config.btnSize)
        .fill('#00000000')
        .stroke(this.editable ? (this.selected ? '#00e5ff' : '#ffcc00') : '#73ffffff')
        .strokeWidth(this.editable ? (this.selected ? 3 : 2) : 1.5)
        .position({ x: this.config.btnSize / 2, y: this.config.btnSize / 2 })

      ForEach(this.buttons, (b: VBtn) => {
        this.Btn(b)
      }, (b: VBtn) => b.slot)
    }
    .width(this.config.diameter)
    .height(this.config.diameter)
    .backgroundColor('#01000000')
    .position({
      x: Math.max(0, Math.min(this.config.xRatio * this.screenW,
                              this.screenW - this.config.diameter)),
      y: Math.max(0, Math.min(this.config.yRatio * this.screenH,
                              this.screenH - this.config.diameter))
    })
    .border(this.editable
      ? { width: 1, color: this.selected ? '#00e5ff' : '#ffcc00', style: BorderStyle.Dashed }
      : undefined)
    .onTouch((e: TouchEvent) => {
      if (this.editable) {
        return;
      }
      if (e.type === TouchType.Down) {
        const t = e.changedTouches[0];
        this.touchId = t.id;
        this.setMask(this.maskFromPoint(t.x, t.y));
      } else if (e.type === TouchType.Move) {
        for (let i = 0; i < e.changedTouches.length; i++) {
          const t = e.changedTouches[i];
          if (t.id === this.touchId) {
            this.setMask(this.maskFromPoint(t.x, t.y));
          }
        }
      } else {
        for (let i = 0; i < e.changedTouches.length; i++) {
          if (e.changedTouches[i].id === this.touchId) {
            this.touchId = -1;
            this.setMask(0);
          }
        }
      }
    })
    .gesture(
      GestureGroup(GestureMode.Parallel,
        TapGesture({ count: 1 })
          .onAction(() => {
            if (!this.editable) {
              return;
            }
            this.onSelect();
          }),
        PanGesture()
          .onActionStart(() => {
            if (!this.editable) {
              return;
            }
            this.onSelect();
            this.baseXRatio = this.config.xRatio;
            this.baseYRatio = this.config.yRatio;
          })
          .onActionUpdate((e: GestureEvent) => {
            if (!this.editable) {
              return;
            }
            if (this.screenW <= 0 || this.screenH <= 0) {
              return;
            }
            const maxX = Math.max(0, 1 - this.config.diameter / this.screenW);
            const maxY = Math.max(0, 1 - this.config.diameter / this.screenH);
            const nx = this.baseXRatio + e.offsetX / this.screenW;
            const ny = this.baseYRatio + e.offsetY / this.screenH;
            this.config = {
              xRatio: Math.max(0, Math.min(maxX, nx)),
              yRatio: Math.max(0, Math.min(maxY, ny)),
              diameter: this.config.diameter,
              btnSize: this.config.btnSize,
            };
          })
      )
    )
  }
}

/** 自由键（照搬 ohosdc FreeButton）：@ObjectLink，编辑态 Tap 选中 / Pan 拖动 */
@Component
struct FreeButton {
  @ObjectLink btn: FreeBtn;
  @Prop editable: boolean = false;
  @Prop screenW: number = 0;
  @Prop screenH: number = 0;
  onPressed: (code: number) => void = () => {};
  onReleased: (code: number) => void = () => {};
  onSelect: () => void = () => {};
  private baseXRatio: number = 0;
  private baseYRatio: number = 0;

  build() {
    Text(this.btn.label)
      .fontSize(this.btn.size * 0.26)
      .fontColor('#f5f5f5')
      .textAlign(TextAlign.Center)
      .width(this.btn.size)
      .height(this.btn.size)
      .borderRadius(this.btn.size / 2)
      .backgroundColor(this.btn.pressed ? PRESS_COLOR : '#66202028')
      .border(this.editable
        ? { width: 2, color: this.btn.selected ? '#00e5ff' : '#ffcc00' }
        : undefined)
      .position({
        x: Math.max(0, Math.min(this.btn.xRatio * this.screenW - this.btn.size / 2,
                                this.screenW - this.btn.size)),
        y: Math.max(0, Math.min(this.btn.yRatio * this.screenH - this.btn.size / 2,
                                this.screenH - this.btn.size))
      })
      .onTouch((e: TouchEvent) => {
        if (this.editable) {
          return;
        }
        if (e.type === TouchType.Down) {
          this.btn.pressed = true;
          this.onPressed(this.btn.code);
        } else if (e.type === TouchType.Up || e.type === TouchType.Cancel) {
          this.btn.pressed = false;
          this.onReleased(this.btn.code);
        }
      })
      .gesture(
        GestureGroup(GestureMode.Parallel,
          TapGesture({ count: 1 })
            .onAction(() => {
              if (!this.editable) {
                return;
              }
              this.onSelect();
            }),
          PanGesture()
            .onActionStart(() => {
              if (!this.editable) {
                return;
              }
              this.onSelect();
              this.baseXRatio = this.btn.xRatio;
              this.baseYRatio = this.btn.yRatio;
            })
            .onActionUpdate((e: GestureEvent) => {
              if (!this.editable) {
                return;
              }
              if (this.screenW <= 0 || this.screenH <= 0) {
                return;
              }
              const maxX = Math.max(0, 1 - this.btn.size / this.screenW);
              const maxY = Math.max(0, 1 - this.btn.size / this.screenH);
              this.btn.xRatio = Math.max(0, Math.min(maxX, this.baseXRatio + e.offsetX / this.screenW));
              this.btn.yRatio = Math.max(0, Math.min(maxY, this.baseYRatio + e.offsetY / this.screenH));
            })
        )
      )
  }
}
