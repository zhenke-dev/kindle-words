#!/usr/bin/env python3
"""把规范源 fsrs-c/{src,include} 同步到固件内的 vendored 副本 firmware/lib/fsrs-c/。

fsrs-c/ 是唯一权威来源；firmware/lib/fsrs-c/ 只是 PlatformIO 需要的拷贝
（无符号链接，PlatformIO lib 目录要求实体文件）。

用法（任意目录）：
  python3 firmware/tools/sync_fsrs.py          # 同步（复制有差异的文件）
  python3 firmware/tools/sync_fsrs.py --check  # 只校验是否一致，不一致退出码 1
                                                # （CI / 提交前用）

注意：同步只影响固件编译，需 pio run -t upload 才会烧进设备；
不涉及 words.bin / LittleFS，不擦学习数据。
"""
import filecmp
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))          # 仓库根
SRC = os.path.join(ROOT, "fsrs-c")
DST = os.path.join(ROOT, "firmware", "lib", "fsrs-c")
# (源子目录, 目标子目录)
PAIRS = [("src", "src"), ("include", "include")]


def files_under(base):
    out = set()
    for dirpath, _dirs, names in os.walk(base):
        for n in names:
            full = os.path.join(dirpath, n)
            out.add(os.path.relpath(full, base))
    return out


def main():
    check_only = "--check" in sys.argv[1:]
    stale, orphan = [], []
    for src_sub, dst_sub in PAIRS:
        sbase = os.path.join(SRC, src_sub)
        dbase = os.path.join(DST, dst_sub)
        sfiles, dfiles = files_under(sbase), files_under(dbase)
        for rel in sorted(sfiles):
            s, d = os.path.join(sbase, rel), os.path.join(dbase, rel)
            if rel not in dfiles or not filecmp.cmp(s, d, shallow=False):
                stale.append((s, d))
                if not check_only:
                    os.makedirs(os.path.dirname(d), exist_ok=True)
                    shutil.copyfile(s, d)
        for rel in sorted(dfiles - sfiles):   # 规范源已删除的文件
            orphan.append(os.path.join(dbase, rel))
            if not check_only:
                os.remove(os.path.join(dbase, rel))

    if not stale and not orphan:
        print("fsrs-c sync: OK (firmware/lib/fsrs-c matches fsrs-c/)")
        return 0

    for s, d in stale:
        print(("STALE  " if check_only else "SYNCED ") + os.path.relpath(d, ROOT)
              + ("  <- " + os.path.relpath(s, ROOT) if check_only else ""))
    for d in orphan:
        print(("ORPHAN " if check_only else "REMOVED ") + os.path.relpath(d, ROOT))

    if check_only:
        print("fsrs-c sync: OUT OF SYNC — run: python3 firmware/tools/sync_fsrs.py")
        return 1
    print("fsrs-c sync: done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
