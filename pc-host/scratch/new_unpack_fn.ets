  /** First launch: stream every bundled resource from rawfile into filesDir
   * so the guest's file layer (ISAAC_DATA_ROOT) can serve it by path. Written
   * in the Dead Cells port's style: getRawFdSync + bounded chunk copy (never
   * whole-file buffers), a size check against the bundled fd so a stale cache
   * re-releases, and progress text pushed to the launcher status line. */
  private async copyResourcesToFilesDir(ctx: common.UIAbilityContext,
                                        rm: resourceManager.ResourceManager,
                                        filesDir: string): Promise<boolean> {
    let totalBundled = 0;
    let totalInSandbox = 0;
    for (const rel of Index.RESOURCE_FILES) {
      try {
        const fd = rm.getRawFdSync(rel);
        const st = fileIo.statSync(filesDir + '/' + rel);
        totalBundled += fd.length;
        if (st.size === fd.length) {
          totalInSandbox += st.size;
        }
      } catch (e) {
      }
    }
    if (totalBundled > 0 && totalInSandbox === totalBundled) {
      this.status = 'resources cached ' + Math.round(totalBundled / 1048576).toString() + ' MB';
      console.info('isaac: resources already unpacked, total=' + totalBundled.toString());
      return true;
    }

    const chunk = 1 << 20;
    const buf = new ArrayBuffer(chunk);
    let done = 0;
    let ok = 0;
    let fail = 0;
    for (const rel of Index.RESOURCE_FILES) {
      const dest = filesDir + '/' + rel;
      try {
        const slash = dest.lastIndexOf('/');
        if (slash > filesDir.length) {
          fileIo.mkdirSync(dest.substring(0, slash), true);
        }
        const src = rm.getRawFdSync(rel);
        const dst = fileIo.openSync(dest,
          fileIo.OpenMode.CREATE | fileIo.OpenMode.WRITE_ONLY | fileIo.OpenMode.TRUNC);
        let start = src.offset;
        let remaining = src.length;
        while (remaining > 0) {
          const n = remaining < chunk ? remaining : chunk;
          const got = fileIo.readSync(src.fd, buf, { offset: start, length: n });
          if (got <= 0) {
            break;
          }
          fileIo.writeSync(dst.fd, buf.slice(0, got));
          start += got;
          remaining -= got;
        }
        fileIo.closeSync(dst);
        fileIo.closeSync(src);
        done += src.length;
        ok++;
        this.status = 'unpacking ' + Math.round(done / 1048576).toString() + ' / '
          + Math.round(totalBundled / 1048576).toString() + ' MB';
      } catch (e) {
        fail++;
        console.error('isaac: unpack failed for ' + rel + ': ' + JSON.stringify(e));
      }
    }
    console.info('isaac: resources unpacked ok=' + ok.toString() + ' fail=' + fail.toString()
      + ' bytes=' + done.toString() + '/' + totalBundled.toString());
    if (fail > 0) {
      this.status = 'unpack: ' + fail.toString() + ' failed (see hilog)';
    }
    return fail === 0;
  }

