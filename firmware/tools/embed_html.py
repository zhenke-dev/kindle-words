"""PlatformIO pre-build 脚本：把 data/www/*.html 内嵌为固件常量。

这样 UI 更新只需烧录固件，无需 uploadfs（uploadfs 会整片擦除 LittleFS，
清空全部学习数据）。words.bin 仍放在 LittleFS，仅词库变化时才 uploadfs。
"""
Import("env")
import os

FILES = [  # (源文件, 常量名)
    ("data/www/index.html", "INDEX_HTML"),
    ("data/www/admin.html", "ADMIN_HTML"),
]

proj = env["PROJECT_DIR"]
dst = os.path.join(proj, "src", "index_html.h")

parts = ["#pragma once",
         "/* 本文件由 tools/embed_html.py 自动生成，请勿手改 */"]
total = 0
for rel, name in FILES:
    with open(os.path.join(proj, rel), encoding="utf-8") as f:
        html = f.read()
    assert ")KWHTML\"" not in html, "raw string delimiter collision"
    parts.append('static const char %s[] PROGMEM = R"KWHTML(%s)KWHTML";'
                 % (name, html))
    total += len(html)

out = "\n".join(parts) + "\n"

stale = True
if os.path.exists(dst):
    with open(dst, encoding="utf-8") as f:
        stale = f.read() != out
if stale:
    with open(dst, "w", encoding="utf-8") as f:
        f.write(out)
    print("[embed_html] src/index_html.h regenerated (%d bytes)" % total)
