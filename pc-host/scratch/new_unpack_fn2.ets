  /** First launch: stream every bundled resource from rawfile into filesDir so
   * the guest's file layer (ISAAC_DATA_ROOT) can serve it by path.
   *
   * Streamed in 4 MiB chunks from the raw fd (never whole-file buffers: the
   * biggest archive is ~385 MB) following the Dead Cells port's pattern. Two
   * device-specific rules learned the hard way:
   *   - raw fds must be released with rm.closeRawFd(name); fileIo.closeSync on
   *     them corrupts the pool and every later open fails 13900008/13900015;
   *   - the release must happen on every path, including errors, so the pool
   *     never runs dry mid-loop.
   * A sentinel file is written only when every entry copied completely, so a
   * half-finished unpack re-runs on the next launch. */
  private async copyResourcesToFilesDir(ctx: common.UIAbilityContext,
                                        rm: resourceManager.ResourceManager,
                                        filesDir: string): Promise<boolean> {
    const sentinel = filesDir + '/resources/.unpacked_v5';
    const scanned = filesDir + '/resources/.unpacked_v4';
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
      console.info('isaac: resources already unpacked (v5 sentinel)');
      return true;
    }
    /* A v4 marker (or nothing) means the cache predates the shader/mount
     * additions or was left half-written: drop it and redo the extraction. */
    try {
      const f = fileIo.openSync(scanned, fileIo.OpenMode.READ_ONLY);
      fileIo.closeSync(f);
      fileIo.unlinkSync(scanned);
    } catch (e) {
    }

    const chunk = 4 * 1048576;
    const buf = new ArrayBuffer(chunk);
    let done = 0;
    let ok = 0;
    let fail = 0;
    let totalBytes = 0;

    for (const rel of Index.RESOURCE_FILES) {
      const dest = filesDir + '/' + rel;
      let opened = false;
      try {
        const slash = dest.lastIndexOf('/');
        if (slash > filesDir.length) {
          fileIo.mkdirSync(dest.substring(0, slash), true);
        }
        const src = rm.getRawFdSync(rel);
        opened = true;
        totalBytes += src.length;
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
          console.error('isaac: unpack short read for ' + rel + ': '
            + copied.toString() + '/' + src.length.toString());
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
      + ' bytes=' + done.toString() + '/' + totalBytes.toString());
    if (fail > 0) {
      this.status = 'unpack: ' + fail.toString() + ' failed (see hilog)';
      return false;
    }
    try {
      const mfd = fileIo.openSync(sentinel,
        fileIo.OpenMode.CREATE | fileIo.OpenMode.WRITE_ONLY | fileIo.OpenMode.TRUNC);
      fileIo.writeSync(mfd.fd, new ArrayBuffer(0));
      fileIo.closeSync(mfd);
    } catch (e) {
    }
    this.status = 'resources ready (' + Math.round(done / 1048576).toString() + ' MB)';
    return true;
  }

