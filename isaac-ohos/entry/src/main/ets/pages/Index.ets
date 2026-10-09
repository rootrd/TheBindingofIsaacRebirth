import native from 'libentry.so';
import { common } from '@kit.AbilityKit';
import { display } from '@kit.ArkUI';
import { fileIo } from '@kit.CoreFileKit';
import { resourceManager } from '@kit.LocalizationKit';
import { preferences } from '@kit.ArkData';

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
const DEFAULT_STICK_RIGHT: PadConfig = { xRatio: 0.84, yRatio: 0.72, diameter: 150, btnSize: 52 };

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
  /* Top row -- same ratios and sizes as the Dead Cells port's 菜单/地图 keys. */
  list.push(new FreeBtn(1, 0x1B, 'PAUSE', 0.01, 0.01, 46));
  list.push(new FreeBtn(2, 0x09, 'MAP', 0.92, 0.01, 46));
  /* Right-hand skill cluster -- the port's 主/副/跳/滚 positions verbatim. */
  list.push(new FreeBtn(3, 0x45, 'BOMB', 0.72, 0.55, 60));
  list.push(new FreeBtn(4, 0x51, 'CARD', 0.82, 0.40, 60));
  list.push(new FreeBtn(5, 0x20, 'ITEM', 0.80, 0.70, 60));
  list.push(new FreeBtn(6, 0x11, 'SWAP', 0.90, 0.55, 60));
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
  /* 拖动式摇杆：按下点即中心，方向由位移决定（不是点圆盘的哪个扇区） */
  private anchorX: number = 0;
  private anchorY: number = 0;

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

  /** 拖动位移 → 方向掩码（8 扇区，死区内的微小位移不激活） */
  private maskFromDrag(dx: number, dy: number): number {
    const dead = this.config.diameter * 0.10;
    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist < dead) {
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
        this.anchorX = t.x;
        this.anchorY = t.y;
        this.setMask(0);
      } else if (e.type === TouchType.Move) {
        for (let i = 0; i < e.changedTouches.length; i++) {
          const t = e.changedTouches[i];
          if (t.id === this.touchId) {
            this.setMask(this.maskFromDrag(t.x - this.anchorX, t.y - this.anchorY));
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

@Entry
@Component
struct Index {
  private xcompController: XComponentController = new XComponentController();
  private started: boolean = false;
  private timerId: number = -1;
  @State surfW: number = 0;   /* px */
  @State surfH: number = 0;
  @State padW: number = 0;    /* vp, for the pad layout */
  @State padH: number = 0;
  @State status: string = 'boot';
  @State fpsText: string = 'FPS --';
  @State backend: string = 'gles';
  /* Launcher state: the game surface stays hidden behind a preparing
   * overlay until the resource unpack finished and the guest was started. */
  @State launched: boolean = false;
  @State busy: boolean = false;

  private static readonly RESOURCE_FILES: string[] = [
    'kage_mount_points.dat',
    'resources/packed/afterbirth.a',
    'resources/packed/afterbirth_jp.a',
    'resources/packed/afterbirth_kr.a',
    'resources/packed/afterbirthp.a',
    'resources/packed/afterbirthp_jp.a',
    'resources/packed/afterbirthp_kr.a',
    'resources/packed/animations.a',
    'resources/packed/config.a',
    'resources/packed/fonts.a',
    'resources/packed/graphics.a',
    'resources/packed/music.a',
    'resources/packed/readme.txt',
    'resources/packed/repentance.a',
    'resources/packed/repentance_de.a',
    'resources/packed/repentance_es.a',
    'resources/packed/repentance_fr.a',
    'resources/packed/repentance_jp.a',
    'resources/packed/repentance_kr.a',
    'resources/packed/repentance_ru.a',
    'resources/packed/repentance_zh.a',
    'resources/packed/rooms.a',
    'resources/packed/sfx.a',
    'resources/packed/videos.a',
    'resources/scripts/enums.lua',
    'resources/scripts/json.lua',
    'resources/scripts/licenses',
    'resources/scripts/linux32/socket/core.so',
    'resources/scripts/linux64/socket/core.so',
    'resources/scripts/main.lua',
    'resources/scripts/mobdebug.lua',
    'resources/scripts/socket.lua',
    'resources/scripts/socket/core.dll',
    'resources/scripts/socket/core.so',
    'resources/scripts/socket/ftp.lua',
    'resources/scripts/socket/http.lua',
    'resources/scripts/socket/smtp.lua',
    'resources/scripts/socket/tp.lua',
    'resources/scripts/socket/url.lua',
    'resources/shaders/bg.fs',
    'resources/shaders/bg.vs',
    'resources/shaders/bloom.fs',
    'resources/shaders/bloom.vs',
    'resources/shaders/colorcorrection.fs',
    'resources/shaders/colorcorrection.vs',
    'resources/shaders/colormod.fs',
    'resources/shaders/colormod.vs',
    'resources/shaders/coloroffset.fs',
    'resources/shaders/coloroffset.vs',
    'resources/shaders/coloroffset_champion.fs',
    'resources/shaders/coloroffset_champion.vs',
    'resources/shaders/coloroffset_dogma.fs',
    'resources/shaders/coloroffset_dogma.vs',
    'resources/shaders/coloroffset_gold.fs',
    'resources/shaders/coloroffset_gold.vs',
    'resources/shaders/dizzy.fs',
    'resources/shaders/dizzy.vs',
    'resources/shaders/hallucination.fs',
    'resources/shaders/hallucination.vs',
    'resources/shaders/heat_wave.fs',
    'resources/shaders/heat_wave.vs',
    'resources/shaders/hq4x.fs',
    'resources/shaders/hq4x.vs',
    'resources/shaders/mirror.fs',
    'resources/shaders/mirror.vs',
    'resources/shaders/oldtv.fs',
    'resources/shaders/oldtv.vs',
    'resources/shaders/pixelation.fs',
    'resources/shaders/pixelation.vs',
    'resources/shaders/shockwave.fs',
    'resources/shaders/shockwave.vs',
    'resources/shaders/water.fs',
    'resources/shaders/water.vs',
    'resources/shaders/water_overlay.fs',
    'resources/shaders/water_overlay.vs',
    'resources/shaders/water_v2.fs',    'shaders/bg.vs',
    'shaders/bloom.fs',
    'shaders/bloom.vs',
    'shaders/colorcorrection.fs',
    'shaders/colorcorrection.vs',
    'shaders/colormod.fs',
    'shaders/colormod.vs',
    'shaders/coloroffset.fs',
    'shaders/coloroffset.vs',
    'shaders/coloroffset_champion.fs',
    'shaders/coloroffset_champion.vs',
    'shaders/coloroffset_dogma.fs',
    'shaders/coloroffset_dogma.vs',
    'shaders/coloroffset_gold.fs',
    'shaders/coloroffset_gold.vs',
    'shaders/dizzy.fs',
    'shaders/dizzy.vs',
    'shaders/hallucination.fs',
    'shaders/hallucination.vs',
    'shaders/heat_wave.fs',
    'shaders/heat_wave.vs',
    'shaders/hq4x.fs',
    'shaders/hq4x.vs',
    'shaders/mirror.fs',
    'shaders/mirror.vs',
    'shaders/oldtv.fs',
    'shaders/oldtv.vs',
    'shaders/pixelation.fs',
    'shaders/pixelation.vs',
    'shaders/shockwave.fs',
    'shaders/shockwave.vs',
    'shaders/water.fs',
    'shaders/water.vs',
    'shaders/water_overlay.fs',
    'shaders/water_overlay.vs',
    'shaders/water_v2.fs',

    'resources/shaders/water_v2.vs'
  ];

  /** Copy the exe out of rawfile into filesDir (the guest's data root). */
  private copyExeToFilesDir(): string {
    const ctx = getContext(this) as common.UIAbilityContext;
    const filesDir = ctx.filesDir;
    const exePath = filesDir + '/isaac-ng.exe';
    try {
      fileIo.unlinkSync(exePath);
    } catch (e) {
    }
    try {
      const rawContent: Uint8Array = ctx.resourceManager.getRawFileContentSync('isaac-ng.exe');
      const fd = fileIo.openSync(exePath, fileIo.OpenMode.CREATE | fileIo.OpenMode.WRITE_ONLY);
      fileIo.writeSync(fd.fd, rawContent.buffer);
      fileIo.closeSync(fd);
      console.info('isaac: copied exe size=' + rawContent.byteLength.toString());
      return filesDir;
    } catch (e) {
      console.error('isaac: failed to copy exe: ' + JSON.stringify(e));
      return '';
    }
  }

  /** First launch: stream every bundled resource into filesDir, in the Dead
   * Cells port's style (4 MiB chunks off the raw fd), with the two device
   * rules that cost a debugging session each: raw fds must be released with
   * rm.closeRawFd(name) (fileIo.closeSync corrupts the pool -> 13900008),
   * and mkdirSync throws 13900015 when the directory already exists. */
  private async copyResourcesToFilesDir(ctx: common.UIAbilityContext,
                                        rm: resourceManager.ResourceManager,
                                        filesDir: string): Promise<boolean> {
    const sentinel = filesDir + '/resources/.unpacked_v7';
    let haveSentinel = false;
    try {
      const f = fileIo.openSync(sentinel, fileIo.OpenMode.READ_ONLY);
      fileIo.closeSync(f);
      haveSentinel = true;
    } catch (e) {
      haveSentinel = false;
    }
    if (haveSentinel) {
      this.status = 'resources cached';
      return true;
    }
    const chunk = 4 * 1048576;
    const buf = new ArrayBuffer(chunk);
    let done = 0;
    let ok = 0;
    let fail = 0;
    for (const rel of Index.RESOURCE_FILES) {
      const dest = filesDir + '/' + rel;
      let opened = false;
      try {
        const slash = dest.lastIndexOf('/');
        if (slash > filesDir.length) {
          try {
            fileIo.mkdirSync(dest.substring(0, slash), true);
          } catch (e0) {
          }
        }
        const src = rm.getRawFdSync(rel);
        opened = true;
        const dst = fileIo.openSync(dest,
          fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
        let copied = 0;
        while (copied < src.length) {
          const want = Math.min(chunk, src.length - copied);
          const got = await fileIo.read(src.fd, buf, { offset: src.offset + copied, length: want });
          if (got <= 0) {
            break;
          }
          fileIo.writeSync(dst.fd, buf, { length: got });
          copied += got;
        }
        fileIo.closeSync(dst);
        rm.closeRawFd(rel);
        opened = false;
        if (copied !== src.length) {
          fail++;
          continue;
        }
        done += src.length;
        ok++;
        this.status = 'unpacking ' + Math.round(done / 1048576).toString() + ' MB';
      } catch (e) {
        fail++;
        if (opened) {
          try {
            rm.closeRawFd(rel);
          } catch (e2) {
          }
        }
        console.error('isaac: unpack failed for ' + rel + ': ' + JSON.stringify(e));
      }
    }
    console.info('isaac: resources unpacked ok=' + ok.toString() + ' fail=' + fail.toString()
      + ' bytes=' + done.toString());
    if (fail > 0) {
      this.status = 'unpack failed (' + fail.toString() + ')';
      return false;
    }
    try {
      const mfd = fileIo.openSync(sentinel,
        fileIo.OpenMode.CREATE | fileIo.OpenMode.WRITE_ONLY | fileIo.OpenMode.TRUNC);
      fileIo.writeSync(mfd.fd, new ArrayBuffer(0));
      fileIo.closeSync(mfd);
    } catch (e) {
    }
    this.status = 'resources ready';
    return true;
  }

  /* ---- virtual pad (Dead Cells style) ---- */
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

  aboutToAppear(): void {
    try {
      const d = display.getDefaultDisplaySync();
      this.surfW = d.width;             /* px: handed to native.init */
      this.surfH = d.height;
      /* Pre-compute the vp size the pads lay out in. Calling px2vp() inside
       * build() read 0 until this state had been applied, which pinned every
       * pad to the top-left corner. */
      this.padW = px2vp(d.width);
      this.padH = px2vp(d.height);
    } catch (e) {
    }
    this.timerId = setInterval(() => {
      if (this.started) {
        this.fpsText = 'FPS ' + native.getFps().toString();
      }
    }, 1000);
  }

  aboutToDisappear(): void {
    if (this.timerId >= 0) {
      clearInterval(this.timerId);
      this.timerId = -1;
    }
  }

  private startRender(): void {
    let attempts = 0;
    const tryStart = async (): Promise<void> => {
      attempts++;
      let sid = '';
      try {
        sid = this.xcompController.getXComponentSurfaceId();
      } catch (e) {
        sid = '';
      }
      if (sid === '') {
        if (attempts < 50) {
          setTimeout(tryStart, 100);
        } else {
          this.status = 'no surfaceId';
        }
        return;
      }
      console.info('isaac sid=' + sid + ' size=' + this.surfW.toString() + 'x' + this.surfH.toString());

      /* Resources, exe and data path were prepared by startGame(); this only
       * binds the surface and starts the guest. */
      const rc: number = native.init(sid, this.surfW, this.surfH, this.backend);
      this.started = (rc === 0);
      if (this.started) {
        this.status = 'running ' + this.backend;
      } else {
        this.status = 'init failed ' + rc.toString();
      }
    };
    tryStart();
  }

  /** Launcher entry: unpack (with progress) then start the guest. The overlay
   * stays up until the guest actually reports started. */
  private async startGame(): Promise<void> {
    if (this.busy || this.launched) {
      return;
    }
    this.busy = true;
    try {
      const ctx = getContext(this) as common.UIAbilityContext;
      this.status = 'preparing resources...';
      const unpacked = await this.copyResourcesToFilesDir(ctx, ctx.resourceManager, ctx.filesDir);
      if (!unpacked) {
        this.status = 'unpack failed (see hilog)';
        return;
      }
      this.status = 'copying exe...';
      const dataPath = this.copyExeToFilesDir();
      if (dataPath === '') {
        this.status = 'exe copy failed';
        return;
      }
      native.setDataPath(dataPath);
      this.status = 'starting guest...';
      this.loadPadLayout();
      this.launched = true;
      this.startRender();
    } finally {
      this.busy = false;
    }
  }

  private switchBackend(b: string): void {
    this.backend = b;
    if (this.started) {
      native.setBackend(b);
      this.status = 'switching ' + b;
    }
  }

  build() {
    Stack() {
      XComponent({ id: 'isaac', type: 'surface', controller: this.xcompController })
        .width('100%')
        .height('100%')
        .onLoad(() => {
          /* Surface exists, but the guest starts only after the launcher
           * begins unpacking -- see startGame(). */
        })
        .onDestroy(() => {
          if (this.started) {
            native.destroy();
            this.started = false;
            this.status = 'destroyed';
          }
        })
        .onTouch((e: TouchEvent) => {
          if (!e.changedTouches || e.changedTouches.length === 0) {
            return;
          }
          const t: TouchObject = e.changedTouches[0];
          const act: number = (e.type === TouchType.Down) ? 0 : ((e.type === TouchType.Move) ? 1 : 2);
          native.touch(t.x, t.y, act, 0);
        })
      /* Launcher overlay: covers the surface until the guest is running. */
      if (!this.launched) {
        Column({ space: 20 }) {
          Text('The Binding of Isaac')
            .fontSize(30)
            .fontColor('#F0E6D2')
            .fontWeight(FontWeight.Bold)
          Text('Repentance - HarmonyOS port')
            .fontSize(14)
            .fontColor('#9A8F7D')
          Text(this.status)
            .fontSize(13)
            .fontColor('#C8BEA8')
            .margin({ top: 8 })
          Button(this.busy ? 'preparing...' : 'START GAME')
            .width(220)
            .height(52)
            .fontSize(18)
            .enabled(!this.busy)
            .backgroundColor(this.busy ? '#555555' : '#8B2F2F')
            .onClick(() => {
              this.startGame();
            })
        }
        .justifyContent(FlexAlign.Center)
        .width('100%')
        .height('100%')
        .backgroundColor('#101010')
      }
      /* Virtual pad (Dead Cells style): two stick discs plus the action
       * keys, only while the game runs. */
      if (this.launched && this.padReady) {
        VirtualPad({
          config: this.stickLeft, buttons: STICK_LEFT_BTNS,
          editable: this.editMode, selected: this.selSide === 'left',
          screenW: this.padW, screenH: this.padH,
          onBtnDown: (code: number) => { native.key(code, 1); },
          onBtnUp: (code: number) => { native.key(code, 0); },
          onSelect: () => { this.selectSide('left'); }
        })
        VirtualPad({
          config: this.stickRight, buttons: STICK_RIGHT_BTNS,
          editable: this.editMode, selected: this.selSide === 'right',
          screenW: this.padW, screenH: this.padH,
          onBtnDown: (code: number) => { native.key(code, 1); },
          onBtnUp: (code: number) => { native.key(code, 0); },
          onSelect: () => { this.selectSide('right'); }
        })
        ForEach(this.pads, (item: FreeBtn) => {
          FreeButton({
            btn: item, editable: this.editMode,
            screenW: this.padW, screenH: this.padH,
            onPressed: (code: number) => { native.key(code, 1); },
            onReleased: (code: number) => { native.key(code, 0); },
            onSelect: () => { this.selSide = ''; this.selId = item.id;
                              item.selected = true; }
          })
        }, (item: FreeBtn) => item.id.toString())
      }

      /* Debug overlay: only meaningful once the guest runs. */
      if (this.launched) {
        Column({ space: 8 }) {
          Text('isaac-ohos [' + this.status + ']')
            .fontSize(13)
            .fontColor('#E0E0E0')
          Text(this.fpsText)
            .fontSize(22)
            .fontColor('#00FF88')
          Row({ space: 8 }) {
            Button('VK')
              .onClick(() => {
                this.switchBackend('vk');
              })
              .backgroundColor(this.backend === 'vk' ? '#2A7FFF' : '#555555')
            Button('GLES')
              .onClick(() => {
                this.switchBackend('gles');
              })
              .backgroundColor(this.backend === 'gles' ? '#2A7FFF' : '#555555')
          }
        }
        .alignItems(HorizontalAlign.Start)
        .align(Alignment.TopStart)
        .margin({ left: 16, top: 16 })
        .hitTestBehavior(HitTestMode.None)
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#101418')
  }
}
