/* ============================================================================
 * Virtual pad — structure copied from the Dead Cells port (ohosdc), with the
 * button catalogue swapped to Isaac's Xbox layout. Everything else is the
 * same design: ratio-based placement, drag-to-move, sizeable pads, per-button
 * persistence via preferences, a dashed edit mode, and stick discs whose
 * touch activates a direction key.
 *
 *   MOVE  stick -> W A S D          BOMB  LB -> E        CARD  RB -> Q
 *   SHOOT stick -> arrow keys       ITEM  LT -> Space    SWAP  RT -> Ctrl
 *   MAP   SELECT -> Tab             PAUSE START -> Esc
 * ========================================================================= */

const PREF_NAME = 'isaac_pad';
const PREF_KEY_PADS = 'pads_v2';
const PAD_SEL_ID: number = -100;

/** 摇杆圆盘（死亡细胞的可调整布局）：位置比例 + 直径 + 按钮尺寸，全部 vp */
interface PadConfig {
  xRatio: number;
  yRatio: number;
  diameter: number;
  btnSize: number;
}

const DEFAULT_STICK: PadConfig = { xRatio: 0.03, yRatio: 0.55, diameter: 200, btnSize: 64 };

/** 自由键记录（死亡细胞 FreeBtnRec）：全量存 code+label+位置+大小 */
interface FreeBtnRec {
  code: number;
  label: string;
  xRatio: number;
  yRatio: number;
  size: number;
}

/** 可映射键位目录：code 用 Windows VK，label 是按钮上的短标签 */
interface KeyDef {
  code: number;
  label: string;
}
const KEY_CATALOG: KeyDef[] = [
  { code: 0x45, label: 'BOMB' },   /* LB  E */
  { code: 0x51, label: 'CARD' },   /* RB  Q */
  { code: 0x20, label: 'ITEM' },   /* LT  Space */
  { code: 0x11, label: 'SWAP' },   /* RT  Ctrl */
  { code: 0x09, label: 'MAP' },    /* SELECT Tab */
  { code: 0x1B, label: 'PAUSE' },  /* START  Esc */
];

/** 摇杆方向 → VK（左盘给 WASD，右盘给方向键） */
const STICK_KEYS_MOVE: number[] = [0x41, 0x44, 0x57, 0x53];
const STICK_KEYS_SHOOT: number[] = [0x25, 0x27, 0x26, 0x28];

const PRESS_COLOR = '#e6ff6a1a';

/** @Observed：拖动时只重渲染该按钮，手势不被 ForEach diff 打断 */
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
  /** 摇杆用：四个方向当前的按下状态（顺序同 STICK_KEYS_*） */
  held: boolean[];

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
    this.held = [false, false, false, false];
  }
}

/** 以撒的手柄键位（对应 Xbox 布局；两个摇杆 + 六个动作键） */
function defaultFreeButtons(): FreeBtn[] {
  const list: FreeBtn[] = [];
  list.push(new FreeBtn(1, 0x45, 'BOMB', 0.72, 0.80, 62));
  list.push(new FreeBtn(2, 0x51, 'CARD', 0.80, 0.66, 62));
  list.push(new FreeBtn(3, 0x20, 'ITEM', 0.64, 0.66, 62));
  list.push(new FreeBtn(4, 0x11, 'SWAP', 0.88, 0.50, 56));
  list.push(new FreeBtn(5, 0x09, 'MAP', 0.97, 0.09, 54));
  list.push(new FreeBtn(6, 0x1B, 'PAUSE', 0.50, 0.06, 54));
  return list;
}

async function saveFreeLayout(context: common.UIAbilityContext, recs: FreeBtnRec[]): Promise<void> {
  try {
    const pref = await preferences.getPreferences(context, PREF_NAME);
    await pref.put(PREF_KEY_PADS, JSON.stringify(recs));
    await pref.flush();
  } catch (e) {
  }
}

async function loadFreeLayout(context: common.UIAbilityContext): Promise<FreeBtnRec[] | null> {
  try {
    const pref = await preferences.getPreferences(context, PREF_NAME);
    const raw = await pref.get(PREF_KEY_PADS, '') as string;
    if (!raw) {
      return null;
    }
    return JSON.parse(raw) as FreeBtnRec[];
  } catch (e) {
    return null;
  }
}

/** 单个摇杆圆盘：死亡细胞的 LeftPad 结构，触摸按方向激活四个键 */
@Component
struct StickPad {
  @ObjectLink cfg: PadConfig;
  @ObjectLink btn: FreeBtn;
  keys: number[] = STICK_KEYS_MOVE;
  private touchId: number = -1;
  private screenW: number = 0;
  private screenH: number = 0;

  private maskFromPoint(x: number, y: number): boolean[] {
    const cx = this.cfg.xRatio * this.screenW + this.cfg.diameter / 2;
    const cy = this.cfg.yRatio * this.screenH + this.cfg.diameter / 2;
    const dead = this.cfg.diameter * 0.12;
    const dx = x - cx;
    const dy = y - cy;
    return [dx < -dead, dx > dead, dy < -dead, dy > dead];
  }

  private applyMask(mask: boolean[]): void {
    for (let i = 0; i < 4; i++) {
      if (mask[i] !== this.btn.held[i]) {
        native.key(this.keys[i], mask[i] ? 1 : 0);
        this.btn.held[i] = mask[i];
      }
    }
  }

  private clearMask(): void {
    this.applyMask([false, false, false, false]);
    this.touchId = -1;
  }

  build() {
    Stack() {
      Circle()
        .width(this.cfg.diameter - this.cfg.btnSize)
        .height(this.cfg.diameter - this.cfg.btnSize)
        .fill('#00000000')
        .stroke(this.btn.edit ? (this.btn.selected ? '#ffcc00' : '#00e5ff') : '#73ffffff')
        .strokeWidth(this.btn.edit ? 3 : 1.5)
        .position({ x: this.cfg.btnSize / 2, y: this.cfg.btnSize / 2 })
      /* 中心指示块：四个方向按下时亮起对应的一瓣 */
      Column()
        .width(this.cfg.btnSize * 0.5)
        .height(this.cfg.btnSize * 0.5)
        .borderRadius(this.cfg.btnSize * 0.25)
        .backgroundColor(this.btn.held[0] || this.btn.held[1] || this.btn.held[2] || this.btn.held[3]
          ? PRESS_COLOR : '#66ffffff')
        .position({ x: (this.cfg.diameter - this.cfg.btnSize * 0.5) / 2,
                    y: (this.cfg.diameter - this.cfg.btnSize * 0.5) / 2 })
    }
    .width(this.cfg.diameter)
    .height(this.cfg.diameter)
    .backgroundColor('#01000000')
    .position({
      x: Math.max(0, Math.min(this.cfg.xRatio * this.screenW, this.screenW - this.cfg.diameter)),
      y: Math.max(0, Math.min(this.cfg.yRatio * this.screenH, this.screenH - this.cfg.diameter))
    })
    .onTouch((e: TouchEvent) => {
      if (this.btn.edit) {
        return;
      }
      if (e.type === TouchType.Down) {
        const t = e.changedTouches[0];
        this.touchId = t.id;
        this.applyMask(this.maskFromPoint(t.x, t.y));
      } else if (e.type === TouchType.Move) {
        for (let i = 0; i < e.changedTouches.length; i++) {
          const t = e.changedTouches[i];
          if (t.id === this.touchId) {
            this.applyMask(this.maskFromPoint(t.x, t.y));
          }
        }
      } else {
        this.clearMask();
      }
    })
  }
}

/** 自由键：按下发键，编辑态可拖动（死亡细胞的 FreeButtons 结构） */
@Component
struct FreeButton {
  @ObjectLink btn: FreeBtn;
  private screenW: number = 0;
  private screenH: number = 0;
  private dragX: number = 0;
  private dragY: number = 0;
  private dragging: boolean = false;

  build() {
    Text(this.btn.label)
      .fontSize(this.btn.size * 0.26)
      .fontColor('#f5f5f5')
      .textAlign(TextAlign.Center)
      .width(this.btn.size)
      .height(this.btn.size)
      .borderRadius(this.btn.size / 2)
      .backgroundColor(this.btn.held[0] ? PRESS_COLOR : '#66202028')
      .borderWidth(this.btn.edit ? 2 : 0)
      .borderColor(this.btn.selected ? '#ffcc00' : '#00e5ff')
      .position({
        x: Math.max(0, Math.min(this.btn.xRatio * this.screenW - this.btn.size / 2,
                                this.screenW - this.btn.size)),
        y: Math.max(0, Math.min(this.btn.yRatio * this.screenH - this.btn.size / 2,
                                this.screenH - this.btn.size))
      })
      .onTouch((e: TouchEvent) => {
        if (this.btn.edit) {
          if (e.type === TouchType.Down) {
            const t = e.changedTouches[0];
            this.dragging = true;
            this.dragX = t.x - this.btn.xRatio * this.screenW;
            this.dragY = t.y - this.btn.yRatio * this.screenH;
            this.btn.selected = true;
          } else if (e.type === TouchType.Move && this.dragging) {
            const t = e.changedTouches[0];
            this.btn.xRatio = (t.x - this.dragX) / this.screenW;
            this.btn.yRatio = (t.y - this.dragY) / this.screenH;
          } else {
            this.dragging = false;
          }
          return;
        }
        if (e.type === TouchType.Down) {
          native.key(this.btn.code, 1);
          this.btn.held[0] = true;
        } else if (e.type === TouchType.Up || e.type === TouchType.Cancel) {
          native.key(this.btn.code, 0);
          this.btn.held[0] = false;
        }
      })
  }
}
