#!/usr/bin/env python3
"""把 book/words.json 预编译为固件用的紧凑二进制词库 data/words.bin。

格式（全部小端）：
  4B   magic "KWDB"
  u16  count
  u32[count] 每条记录的绝对偏移
  记录: u8 word_len, word, u16 meaning_len, meaning   (UTF-8，无 NUL)
"""
import json
import struct
import sys
import os

SRC = os.path.join(os.path.dirname(__file__), "..", "..", "book", "words.json")
DST = os.path.join(os.path.dirname(__file__), "..", "data", "words.bin")

def main():
    with open(SRC, encoding="utf-8") as f:
        words = json.load(f)
    records = []
    for item in words:
        w = item["word"].strip().encode("utf-8")
        m = item["meaning"].strip().encode("utf-8")
        assert 0 < len(w) < 256, item["word"]
        assert 0 < len(m) < 65536, item["word"]
        records.append(struct.pack("<B", len(w)) + w + struct.pack("<H", len(m)) + m)

    header_size = 6 + 4 * len(records)
    offsets = []
    pos = header_size
    for r in records:
        offsets.append(pos)
        pos += len(r)

    os.makedirs(os.path.dirname(DST), exist_ok=True)
    with open(DST, "wb") as f:
        f.write(b"KWDB")
        f.write(struct.pack("<H", len(records)))
        for off in offsets:
            f.write(struct.pack("<I", off))
        for r in records:
            f.write(r)
    print("words: %d, size: %d bytes -> %s" % (len(records), pos, DST))

if __name__ == "__main__":
    sys.exit(main())
