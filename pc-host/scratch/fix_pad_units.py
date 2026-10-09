#!/usr/bin/env python3
"""The pad data is in device pixels (the surface size), but ArkTS lays out in
vp -- passing raw pixels put every pad metres off-screen. Convert at the
display boundary only: sizes in build(), and the position in PadView."""
import io

PAGE = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/ets/pages/Index.ets'
src = io.open(PAGE, encoding='utf-8').read()

# --- sizes: wrap every layout use of btn.size in px2vp() -------------------
replacements = [
    ('.fontSize(this.btn.size * 0.26)', '.fontSize(px2vp(this.btn.size) * 0.26)'),
    ('          .width(this.btn.size)\n          .height(this.btn.size)\n          .borderRadius(this.btn.size / 2)',
     '          .width(px2vp(this.btn.size))\n          .height(px2vp(this.btn.size))\n          .borderRadius(px2vp(this.btn.size) / 2)'),
    ('          .width(this.btn.size * 0.42)\n          .height(this.btn.size * 0.42)\n          .borderRadius(this.btn.size * 0.21)',
     '          .width(px2vp(this.btn.size) * 0.42)\n          .height(px2vp(this.btn.size) * 0.42)\n          .borderRadius(px2vp(this.btn.size) * 0.21)'),
    ('    .width(this.btn.size)\n    .height(this.btn.size)\n    .borderRadius(this.btn.size / 2)',
     '    .width(px2vp(this.btn.size))\n    .height(px2vp(this.btn.size))\n    .borderRadius(px2vp(this.btn.size) / 2)'),
    ('    .position({\n      x: this.availX + this.btn.xRatio * this.availW - this.btn.size / 2,\n      y: this.availY + this.btn.yRatio * this.availH - this.btn.size / 2\n    })',
     '    .position({\n      x: px2vp(this.availX + this.btn.xRatio * this.availW - this.btn.size / 2),\n      y: px2vp(this.availY + this.btn.yRatio * this.availH - this.btn.size / 2)\n    })'),
]
for old, new in replacements:
    if old not in src:
        print('MISS:', old[:60].replace('\n', '|'))
        continue
    src = src.replace(old, new, 1)

io.open(PAGE, 'w', encoding='utf-8').write(src)
print('units fixed')
