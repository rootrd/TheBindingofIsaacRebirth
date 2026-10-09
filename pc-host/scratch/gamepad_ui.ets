/* ---------------------------------------------------------------- input ---
 * Touch overlay modelled on the Xbox pad layout the wiki documents:
 *   left stick  -> move        (W A S D)
 *   right stick -> shoot       (arrow keys; the A/B/X/Y face buttons double as
 *                               the same four directions on a real pad)
 *   LB -> bomb (E)   RB -> card/pill (Q)   LT -> active item (Space)
 *   RT -> swap/drop (Left Ctrl)   START -> pause (Esc)   SELECT -> map (Tab)
 *
 * Sticks are relative: touch anywhere in the left half and drag -- the press
 * point becomes the stick centre, so no fixed stick position is needed and
 * two fingers can drive move+shoot at once. */

const VK_LEFT = 0x25;
const VK_UP = 0x26;
const VK_RIGHT = 0x27;
const VK_DOWN = 0x28;
const VK_A = 0x41;
const VK_D = 0x44;
const VK_W = 0x57;
const VK_S = 0x53;
const VK_E = 0x45;      /* LB: bomb */
const VK_Q = 0x51;      /* RB: card / pill */
const VK_SPACE = 0x20;  /* LT: active item */
const VK_CTRL = 0x11;   /* RT: swap / drop (hold) */
const VK_TAB = 0x09;    /* SELECT: map */
const VK_ESC = 0x1B;    /* START: pause */

const MOVE_KEYS: number[] = [VK_A, VK_D, VK_W, VK_S];
const SHOOT_KEYS: number[] = [VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN];

class Stick {
  id: number = -1;
  cx: number = 0;
  cy: number = 0;
  held: boolean[] = [false, false, false, false];
}

@Entry
@Component
struct Index {
  /* --- input state (plain members: no re-render needed) --- */
  private moveStick: Stick = new Stick();
  private shootStick: Stick = new Stick();
  private stickRadius: number = 0;

  private setStickDirection(stick: Stick, keys: number[], dx: number, dy: number): void {
    const dead = this.stickRadius * 0.28;
    const want: boolean[] = [dx < -dead, dx > dead, dy < -dead, dy > dead];
    for (let i = 0; i < 4; i++) {
      if (want[i] !== stick.held[i]) {
        native.key(keys[i], want[i] ? 1 : 0);
        stick.held[i] = want[i];
      }
    }
  }

  private releaseStick(stick: Stick, keys: number[]): void {
    for (let i = 0; i < 4; i++) {
      if (stick.held[i]) {
        native.key(keys[i], 0);
        stick.held[i] = false;
      }
    }
    stick.id = -1;
  }

  private handleSticks(e: TouchEvent): void {
    if (this.surfW <= 0 || this.surfH <= 0) {
      return;
    }
    if (this.stickRadius <= 0) {
      this.stickRadius = this.surfW * 0.09;
    }
    const touches = e.changedTouches;
    for (let i = 0; i < touches.length; i++) {
      const t = touches[i];
      const isMoveSide = t.x < this.surfW / 2;
      const stick = isMoveSide ? this.moveStick : this.shootStick;
      const keys = isMoveSide ? MOVE_KEYS : SHOOT_KEYS;
      if (e.type === TouchType.Down) {
        /* New press on this side takes over the stick. */
        this.releaseStick(stick, keys);
        stick.id = t.id;
        stick.cx = t.x;
        stick.cy = t.y;
        /* A tap in place with no drag is the common case for a d-pad-like
         * press: treat the touch point itself as the direction so a simple
         * tap already moves. */
        this.setStickDirection(stick, keys, 0, 0);
      } else if (e.type === TouchType.Move) {
        if (stick.id === t.id) {
          this.setStickDirection(stick, keys, t.x - stick.cx, t.y - stick.cy);
        }
      } else {
        if (stick.id === t.id) {
          this.releaseStick(stick, keys);
        }
      }
    }
  }

  @Builder
  PadButton(label: string, vk: number) {
    Text(label)
      .fontSize(15)
      .fontColor('#F0F0F0')
      .textAlign(TextAlign.Center)
      .width(52)
      .height(52)
      .borderRadius(26)
      .backgroundColor('#66000000')
      .borderWidth(1)
      .borderColor('#55FFFFFF')
      .onTouch((e: TouchEvent) => {
        if (e.type === TouchType.Down) {
          native.key(vk, 1);
        } else if (e.type === TouchType.Up || e.type === TouchType.Cancel) {
          native.key(vk, 0);
        }
      })
  }

  @Builder
  GamepadOverlay() {
    /* LB / RB / LT / RT on the right edge, START / SELECT on the top edge --
     * the same physical arrangement as an Xbox pad held in landscape. */
    Column({ space: 10 }) {
      Row({ space: 10 }) {
        this.PadButton('BOMB', VK_E)
        this.PadButton('CARD', VK_Q)
      }
      Row({ space: 10 }) {
        this.PadButton('ITEM', VK_SPACE)
        this.PadButton('SWAP', VK_CTRL)
      }
    }
    .align(Alignment.BottomEnd)
    .margin({ right: 18, bottom: 18 })

    Row({ space: 10 }) {
      this.PadButton('MAP', VK_TAB)
      this.PadButton('PAUSE', VK_ESC)
    }
    .align(Alignment.TopEnd)
    .margin({ right: 18, top: 18 })
  }
}
