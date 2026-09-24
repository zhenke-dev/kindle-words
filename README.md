# KindleWords

为 Kindle Paperwhite（第 12 代 / KPW6，固件 5.19.6）电子墨水屏定制的背单词应用。
服务端运行于 **ESP32-S3-N16R8** 开发板（WiFi AP 模式），Kindle 连接热点后通过内置
浏览器访问；记忆调度使用 **FSRS-6** 算法（纯 C 实现，见 `fsrs-c/`），全部计算在服务端
完成，Kindle 端只负责渲染。

---

## 特性

- **FSRS-6 记忆调度**：21 参数推理，含同日（t=0）短期模型；评分"重来/困难"的卡片
  会进入今日重学队列，间隔 2 张后于同次学习中重现
- **专为 KPW6 浏览器优化**：严格 ES2015 语法（内核约 Chromium 75–80，JIT 禁用）、
  无动画/无图片/极简 DOM、每次评分仅 1 次 HTTP 往返（响应直接携带下一张卡片）
- **学习数据闭环**：每次评分追加 CSV 日志，PC 端管理页可导出，用于离线训练优化
  FSRS 参数；训练好的 21 个权重可粘贴回传更新（带参数代数管理）
- **新词顺序**：按词书顺序 / 随机（硬件随机数抽取），可随时切换
- **异步并发 HTTP 服务**（ESPAsyncWebServer）：Kindle 后台探测请求不会阻塞学习流量

## 目录结构

```
KindleWords/
├── book/words.json       # 内置词书（6550 词）
├── docx/                 # 项目相关文档
├── fsrs-c/               # FSRS-6 算法 C 实现（带 144 项自测）
└── firmware/             # ESP32 固件（PlatformIO 工程）
    ├── platformio.ini    # espressif32@6.8.1 / 16MB Flash / OPI PSRAM
    ├── partitions.csv    # 3MB 应用 + ~12.9MB LittleFS
    ├── tools/
    │   ├── build_words.py  # words.json → data/words.bin（紧凑二进制词库）
    │   └── embed_html.py   # pre-build：把 data/www/*.html 内嵌为固件常量
    ├── lib/fsrs-c/         # 移植进固件的 FSRS-6 源码
    ├── src/
    │   ├── main.cpp        # WiFi AP + 捕获式 DNS + HTTP API
    │   ├── wordbook.cpp    # words.bin 只读访问
    │   ├── store.cpp       # LittleFS 持久化（分段状态/元数据/参数/CSV 日志）
    │   └── scheduler.cpp   # 复习队列 + 今日重学队列 + FSRS-6 调度
    └── data/
        ├── words.bin       # 词库（构建于 tools/build_words.py）
        └── www/            # index.html（Kindle 端）+ admin.html（PC 管理页）
```

## 构建与烧录

```bash
cd firmware

# 词库变化时重新生成 data/words.bin
python3 tools/build_words.py

# 编译并烧录固件（日常更新只跑这一步，不影响学习数据）
pio run -t upload

# 上传文件系统镜像（仅词库变化时需要！会整片擦除 LittleFS、清空学习数据）
pio run -t uploadfs
```

> **注意**：`uploadfs` 会擦除整个 LittleFS 分区。UI 页面已内嵌进固件
> （`tools/embed_html.py` 在每次构建时自动生成 `src/index_html.h`），
> 因此日常修改 UI/固件逻辑只需 `pio run -t upload`，学习数据安全。

## 使用

| 端 | 入口 | 功能 |
|---|---|---|
| Kindle | `http://192.168.4.1` | 学习、设置（学习计划/参数重置/数据重置） |
| PC | `http://192.168.4.1/admin` | 数据概览、CSV 导出、FSRS 参数更新/重置 |

- 热点：`KindleWords`，密码 `kindle1234`（`src/main.cpp` 顶部可改）
- 捕获式 DNS：所有域名解析到 192.168.4.1（让 Kindle 联网检测通过，WiFi 保持连接）
- **时间基准**：ESP32 无 RTC，由客户端请求携带当前日期（unix day）用于跨天间隔
  计算与日志时间戳，请确保 Kindle 系统时间准确

## 学习逻辑

- 队列优先级：已间隔开的重学卡 → 到期复习卡（最早到期优先）→ 新卡（在每日额度内）
- 评分"重来/困难" → 进入今日重学队列（RAM，跨天清空），间隔 ≥2 张后重现，
  再次评分走 FSRS-6 同日短期模型；"良好/简单"当日毕业
- 每日新卡额度默认 20（设置可调，上限 200）
- 卡片状态常驻 PSRAM（20 字节/卡 × 6550 ≈ 131KB），按 512 卡分段（13 段）持久化，
  每 10 次复习或 30 秒节流写回，只写脏段

## HTTP API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/next?day=N` | 下一张卡片（含释义与四档评分间隔预测） |
| POST | `/api/rate` | 评分 `{id, rating, day, ts}`，响应同 `/api/next` |
| GET | `/api/stats` | 学习统计 |
| GET | `/api/export.csv` | 下载全部复习记录 |
| GET/POST | `/api/params` | 读取 / 更新 21 个 FSRS 权重（有限性校验） |
| POST | `/api/settings` | `{new_per_day, dr, order}` |
| POST | `/api/reset_data` | 清除全部学习数据（保留 FSRS 参数） |
| POST | `/api/reset_params` | FSRS 参数恢复默认（代数归零） |

## 数据格式

**复习日志 `/reviews.csv`**（参数优化训练数据）：

```
ts,day,word_id,word,rating,elapsed_days,s_before,d_before,s_after,d_after,interval
```

`rating`：1 重来 / 2 困难 / 3 良好 / 4 简单；`s/d` 为 FSRS 稳定性与难度。

**持久化文件（LittleFS）**：`/s00`–`/s12` 分段卡片状态、`/meta.bin` 学习进度元数据、
`/params.bin` FSRS 参数与学习计划、`/reviews.csv` 复习日志。

## 已知限制

- KPW6 浏览器抑制 `confirm()` 弹窗 → 重置类操作使用"再次点击确认"两段式
- KPW6 浏览器不支持 flex `gap`（Chromium < 84）→ 按钮间距用兄弟选择器 margin
- ESP32 WiFi 省电已关闭（`WiFi.setSleep(false)`），功耗略高但响应延迟稳定
