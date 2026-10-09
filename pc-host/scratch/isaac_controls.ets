/* ============================================================================
 * Touch controls, modelled on the Dead Cells port's free-button system:
 * draggable pads, persisted per button, and two analog sticks because Isaac
 * is a twin-stick game. The button catalogue below is Isaac's Xbox layout
 * (wiki: 基本操作) with the Dead Cells-specific keys removed.
 *
 *   MOVE   left stick  -> W A S D
 *   SHOOT  right stick -> arrow keys (the pad's face buttons are the same
 *                         four directions, so an analog stick covers them)
 *   BOMB   LB  -> E
 *   CARD   RB  -> Q
 *   ITEM   LT  -> Space
 *   SWAP   RT  -> Left Ctrl (hold to drop)
 *   MAP    SELECT -> Tab
 *   PAUSE  START  -> Esc
 * ========================================================================= */

const VK_LEFT = 0x25;
const VK_UP = 0x26;
const VK_RIGHT = 0x27;
const VK_DOWN = 0x28;
const VK_A = 0x41;
const VK_D = 0x44;
const VK_W = 0x57;
const VK_S = 0x53;
const VK_E = 0x45;
const VK_Q = 0x51;
const VK_SPACE = 0x20;
const VK_CTRL = 0x11;
const VK_TAB = 0x09;
const VK_ESC = 0x1B;

const PREF_NAME = 'isaac_pad';
const PREF_KEY_PADS = 'pads_v1';

/* kind: 0 = plain button, 1 = move stick (WASD), 2 = shoot stick (arrows) */
const KIND_BUTTON = 0;
const KIND_MOVE = 1;
const KIND_SHOOT = 2;

interface PadRec {
  code: number;
  label: string;
  kind: number;
  xRatio: number;
  yRatio: number;
  size: number;
}

/** @Observed so an @ObjectLink child re-renders alone while dragging; a plain
 * object array would rebuild the whole ForEach each frame and break the
 * gesture. id is the stable ForEach key -- never derived from position/size. */
@Observed
class FreeBtn {
  id: number;
  code: number;
  label: string;
  kind: number;
  xRatio: number;
  yRatio: number;
  size: number;
  selected: boolean;
  edit: boolean;
  held: boolean[];

  constructor(id: number, code: number, label: string, kind: number,
              xRatio: number, yRatio: number, size: number) {
    this.id = id;
    this.code = code;
    this.label = label;
    this.kind = kind;
    this.xRatio = xRatio;
    this.yRatio = yRatio;
    this.size = size;
    this.selected = false;
    this.edit = false;
    this.held = [false, false, false, false];
  }
}

/** Isaac's pad, in landscape ratios (x/y are centre positions). */
function defaultPads(): FreeBtn[] {
  const list: FreeBtn[] = [];
  list.push(new FreeBtn(1, 0, 'MOVE', KIND_MOVE, 0.14, 0.72, 200));
  list.push(new FreeBtn(2, 0, 'SHOOT', KIND_SHOOT, 0.86, 0.72, 200));
  list.push(new FreeBtn(3, VK_E, 'BOMB', KIND_BUTTON, 0.70, 0.82, 62));
  list.push(new FreeBtn(4, VK_Q, 'CARD', KIND_BUTTON, 0.78, 0.72, 62));
  list.push(new FreeBtn(5, VK_SPACE, 'ITEM', KIND_BUTTON, 0.70, 0.62, 62));
  list.push(new FreeBtn(6, VK_CTRL, 'SWAP', KIND_BUTTON, 0.86, 0.52, 56));
  list.push(new FreeBtn(7, VK_TAB, 'MAP', KIND_BUTTON, 0.96, 0.10, 54));
  list.push(new FreeBtn(8, VK_ESC, 'PAUSE', KIND_BUTTON, 0.50, 0.07, 54));
  return list;
}

async function savePads(context: common.UIAbilityContext, pads: FreeBtn[]): Promise<void> {
  try {
    const pref = await preferences.getPreferences(context, PREF_NAME);
    const recs: PadRec[] = [];
    for (let i = 0; i < pads.length; i++) {
      const p = pads[i];
      recs.push({ code: p.code, label: p.label, kind: p.kind,
                  xRatio: p.xRatio, yRatio: p.yRatio, size: p.size });
    }
    await pref.put(PREF_KEY_PADS, JSON.stringify(recs));
    await pref.flush();
  } catch (e) {
  }
}

async function loadPads(context: common.UIAbilityContext): Promise<PadRec[] | null> {
  try {
    const pref = await preferences.getPreferences(context, PREF_NAME);
    const raw = await pref.get(PREF_KEY_PADS, '') as string;
    if (!raw) {
      return null;
    }
    return JSON.parse(raw) as PadRec[];
  } catch (e) {
    return null;
  }
}

/** One pad. Owns its own touch handling: a stick resolves drag direction into
 * four keys, a button is a plain key press. Position comes from the parent's
 * ratios so dragging the pad in edit mode moves the whole thing. */
@Component
struct PadView {
  @ObjectLink btn: FreeBtn;
  private availW: number = 0;
  private availH: number = 0;
  private availX: number = 0;
  private availY: number = 0;

  private setStick(keys: number[], dx: number, dy: number): void {
    const dead = this.btn.size * 0.14;
    const want: boolean[] = [dx < -dead, dx > dead, dy < -dead, dy > dead];
    for (let i = 0; i < 4; i++) {
      if (want[i] !== this.btn.held[i]) {
        native.key(keys[i], want[i] ? 1 : 0);
        this.btn.held[i] = want[i];
      }
    }
  }

  private releaseAll(keys: number[]): void {
    for (let i = 0; i < 4; i++) {
      if (this.btn.held[i]) {
        native.key(keys[i], 0);
        this.btn.held[i] = false;
      }
    }
  }

  private stickKeys(): number[] {
    return this.btn.kind === KIND_MOVE
      ? [VK_A, VK_D, VK_W, VK_S]
      : [VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN];
  }

  build() {
    Stack() {
      if (this.btn.kind === KIND_BUTTON) {
        Text(this.btn.label)
          .fontSize(this.btn.size * 0.26)
          .fontColor('#F2F2F2')
          .textAlign(TextAlign.Center)
      } else {
        /* Sticks draw a ring; the knob stays centred (the drag is relative,
         * so there is no visual offset to track). */
        Column()
          .width(this.btn.size)
          .height(this.btn.size)
          .borderRadius(this.btn.size / 2)
          .backgroundColor('#33FFFFFF')
          .borderWidth(2)
          .borderColor('#77FFFFFF')
        Column()
          .width(this.btn.size * 0.42)
          .height(this.btn.size * 0.42)
          .borderRadius(this.btn.size * 0.21)
          .backgroundColor('#88FFFFFF')
      }
    }
    .width(this.btn.size)
    .height(this.btn.size)
    .borderRadius(this.btn.size / 2)
    .backgroundColor(this.btn.kind === KIND_BUTTON ? '#59000000' : '#22000000')
    .borderWidth(this.btn.kind === KIND_BUTTON ? 1 : 0)
    .borderColor(this.btn.selected ? '#FFCC33' : '#55FFFFFF')
    .position({
      x: this.availX + this.btn.xRatio * this.availW - this.btn.size / 2,
      y: this.availY + this.btn.yRatio * this.availH - this.btn.size / 2
    })
    .onTouch((e: TouchEvent) => {
      if (this.btn.edit) {
        return;
      }
      if (this.btn.kind === KIND_BUTTON) {
        if (e.type === TouchType.Down) {
          native.key(this.btn.code, 1);
        } else if (e.type === TouchType.Up || e.type === TouchType.Cancel) {
          native.key(this.btn.code, 0);
        }
        return;
      }
      const t: TouchObject = e.touches[0];
      const keys = this.stickKeys();
      if (e.type === TouchType.Down) {
        this.anchorX = t.x;
        this.anchorY = t.y;
        this.releaseAll(keys);
      } else if (e.type === TouchType.Move) {
        this.setStick(keys, t.x - this.anchorX, t.y - this.anchorY);
      } else {
        this.releaseAll(keys);
      }
    })
  }

  private anchorX: number = 0;
  private anchorY: number = 0;
}
