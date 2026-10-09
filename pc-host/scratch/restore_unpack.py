#!/usr/bin/env python3
"""Re-add the RESOURCE_FILES list and the two unpack helpers that the controls
integration script removed by accident. The list is generated from what is
actually in rawfile, so it can never drift from the HAP."""
import io
import os
import subprocess

PAGE = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/ets/pages/Index.ets'
RAW = '/mnt/f/TheBindingofIsaacRebirth/isaac-ohos/entry/src/main/resources/rawfile'

files = subprocess.check_output(
    ['bash', '-c', "cd '%s' && find . -type f | sed 's|^\\./||' | sort" % RAW]
).decode('utf-8').split()

entries = [f for f in files if f != 'isaac-ng.exe']
listing = ',\n'.join("    '%s'" % f for f in entries)

block = """  private static readonly RESOURCE_FILES: string[] = [
%s
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
    const sentinel = filesDir + '/resources/.unpacked_v5';
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

""" % listing

src = io.open(PAGE, encoding='utf-8').read()
anchor = '  /* ---- free pads (Dead Cells style) ---- */'
assert anchor in src, 'anchor missing'
src = src.replace(anchor, block + anchor, 1)
io.open(PAGE, 'w', encoding='utf-8').write(src)
print('restored, files listed =', len(entries))
