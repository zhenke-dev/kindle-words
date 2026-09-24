# AGENTS.md

ESP32-S3 + Kindle Paperwhite (KPW6) vocabulary app. All compute runs on the ESP32
(WiFi AP); the Kindle only renders. Memory scheduling is FSRS-6 in pure C (`fsrs-c/`).
Docs and code comments are in Chinese — keep new ones consistent.

No CI, no linter, no formatter, no typechecker. There is no app-level test suite;
the only tests are in `fsrs-c/`.

## Layout

- `book/words.json` — word list source (6550 words), input to the binary wordbook
- `fsrs-c/` — **canonical** FSRS-6 C implementation (C11, libm only) + tests
- `firmware/` — PlatformIO project (espressif32@6.8.1, Arduino, 16MB flash, LittleFS)
  - `src/main.cpp` — WiFi AP + captive DNS + all HTTP routes (real entrypoint)
  - `src/scheduler.cpp`, `src/store.cpp`, `src/wordbook.cpp` — queue/FSRS glue,
    LittleFS persistence, wordbook reader
  - `data/www/index.html` (Kindle UI) + `data/www/admin.html` (PC admin page)
  - `lib/fsrs-c/` — vendored copy of the FSRS source (see gotcha below)
- `docx/` — empty

## Commands

Firmware (run from `firmware/`):

```bash
python3 tools/build_words.py   # ONLY after editing book/words.json → regenerates data/words.bin
python3 tools/sync_fsrs.py     # after editing fsrs-c/ → updates the firmware copy
pio run                        # compile only — use this to verify changes
pio run -t upload              # build + flash (upload_port hardcoded to /dev/ttyACM0)
pio run -t uploadfs            # ⚠ erases ALL learning data on LittleFS
```

FSRS library (run from `fsrs-c/`):

```bash
make           # build/libfsrs.a
make test      # 144 self-checks — fast; run after any fsrs-c change
make crosscheck # C vs independent Python reference (needs python3); run for formula changes
```

Order that matters: after changing FSRS logic, `make test && make crosscheck` in
`fsrs-c/`, **then** `python3 tools/sync_fsrs.py` (from repo root), **then** `pio run`.
`sync_fsrs.py --check` exits 1 on drift — use it to verify the copies match.

## Gotchas (will bite you)

- **Two copies of the FSRS source.** `firmware/lib/fsrs-c/` is a plain copy (no
  symlink — PlatformIO's lib dir needs real files) of `fsrs-c/src/` +
  `fsrs-c/include/`. `fsrs-c/` is canonical. Sync with
  `python3 tools/sync_fsrs.py`; `--check` detects drift (exit 1).
- **`uploadfs` wipes learning data.** It rewrites the whole LittleFS partition
  (`/s00`–`/s12`, `/meta.bin`, `/params.bin`, `/reviews.csv`). Only needed when
  `data/words.bin` changes. UI pages are NOT on LittleFS: the pre-build script
  `tools/embed_html.py` (registered in `platformio.ini` `extra_scripts`) embeds
  `data/www/*.html` into `src/index_html.h`, so UI edits ship with a plain
  `pio run -t upload`.
- **`src/index_html.h` is generated** — never hand-edit it; edit `data/www/*.html`.
  (It is committed to git; committing the regenerated version is fine.)
- **`build_words.py` is not a build step** — it must be run by hand after editing
  `book/words.json`, otherwise `firmware/data/words.bin` (and the flashed wordbook)
  stays stale. Note it writes to `firmware/data/words.bin` even though it lives in
  `firmware/tools/`.
- **`.pio/` is gitignored** — deps are NOT vendored; `pio run` fetches
  `lib_deps` from `platformio.ini` on first build (needs network once).
- **KPW6 browser constraints** (Chromium ~75–80, JIT disabled) — hard limits, not style:
  - ES2015 syntax only; no optional chaining, no `gap` in flexbox (< Chromium 84),
    no `confirm()` dialogs (reset actions use a two-click confirm pattern).
  - No animations, no images, minimal DOM; keep each rating to 1 HTTP round-trip
    (the `/api/rate` response already carries the next card).
- **No RTC on the ESP32**: the client sends the current unix day in every API call
  (`?day=N`, body field `day`). Cross-day scheduling and log timestamps depend on it.
- Captive-portal probes (`/generate_204`, `/hotspot-detect.html`, …) are answered
  with 204 and all DNS resolves to 192.168.4.1 — the Kindle drops WiFi otherwise.
  Don't "simplify" these routes away.

## FSRS scope

`fsrs-c/` does inference/scheduling only — no parameter optimizer. The 21 weights are
trained externally (CSV export via `/api/export.csv`) and pasted back via `/api/params`.
Semantics are aligned to fsrs-rs FSRS-6; `fsrs-c/README.md` §2, §6, §7 is the
authoritative spec for formulas, numeric behavior, and known deviations — read it
before touching `fsrs_core.c`.
