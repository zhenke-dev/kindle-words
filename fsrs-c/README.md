# FSRS-6 in C

用纯 C（C11，仅依赖 libm）实现的 **FSRS-6** 记忆算法。算法语义对齐
[`fsrs-rs`](https://github.com/open-spaced-repetition/fsrs-rs) 的 FSRS-6 推理路径，
对外 ABI 参照 [`rs-fsrs-c`](https://github.com/open-spaced-repetition/rs-fsrs-c) 的
C FFI 组织方式（`fsrs_` 前缀、opaque handle、显式 create/destroy、整数错误码）。
**不链接 Rust 运行时**，可直接静态链接进任何 C/C++ 工程。

范围：只做**推理与调度**（给定记忆状态与评分，算出新的记忆状态和下次间隔），
不含参数优化器（optimizer）。参数优化是从复习日志上做数值拟合的另一套工程，
fsrs-rs 单独提供；本库接受任意 21 个权重，因此优化结果可以外部训练好后喂进来。

---

## 1. 快速开始

```bash
make            # 构建 build/libfsrs.a
make shared     # 构建 build/libfsrs.so（共享库，可选）
make test       # 144 项自测（参考向量 + 不变量 + 数值边界 + FFI 契约）
make crosscheck # 与独立 Python 参考实现逐项比对 1124 组状态转移
make example    # 运行调度示例
```

最小用法（对应 fsrs-rs 的 `examples/schedule.rs`）：

```c
#include "fsrs.h"

struct fsrs_scheduler *fsrs = fsrs_scheduler_new(NULL, 0, 0.9f, NULL); /* 默认参数 */
struct fsrs_next_states ns;

fsrs_scheduler_next_states(fsrs, NULL, 0, &ns);   /* 新卡，current 传 NULL */
struct fsrs_memory_state state = *fsrs_next_states_pick(&ns, FSRS_RATING_GOOD);

fsrs_scheduler_next_states(fsrs, &state, 2, &ns); /* 2 天后再调度 */
uint32_t days = ns.interval_good;                 /* 用户按 Good 的间隔 */

fsrs_scheduler_free(fsrs);                        /* 与 new 配对，只调用一次 */
```

不想要 handle 时，可只用无状态内核（无分配、可并发）：

```c
struct fsrs_next_states ns;
fsrs_next_states(NULL, 0.9f, &state, 2, &ns);     /* w 传 NULL 即用默认参数 */
float r = fsrs_retrievability(NULL, state.stability, 2.0f);
```

---

## 2. 算法规范（实现所依据的公式）

参数记为 `w[0..20]`，默认值为 fsrs-rs 的 `FSRS6_DEFAULT_PARAMETERS`：

```
0.212, 1.2931, 2.3065, 8.2956, 6.4133, 0.8334, 3.0194, 0.001, 1.8722, 0.1666,
0.796, 1.4835, 0.0614, 0.2629, 1.6483, 0.6014, 1.8729, 0.5425, 0.0912, 0.0658, 0.1542
```

约定：`G` 为评分（Again=1, Hard=2, Good=3, Easy=4）；`S` 稳定性（天）；
`D` 难度，恒在 `[1,10]`；`R` 可提取性。常量 `S_MIN = 0.01`、`S_MAX = 36500`。

### 遗忘曲线（可提取性）

```
decay  = -w[20]                                  // = -0.1542
factor = 0.9^(1/decay) - 1                       // = 0.98034649
R(t,S) = (1 + factor · t / S)^decay
```

`factor` 在对数空间计算并把指数夹到 60，避免 `|1/decay|` 很大时 `exp()` 上溢
（与 `srs-benchmark/models/fsrs_v6.py` 的 `clamp(max=60)` 一致）。
`t` 先 `max(0)` 再取整（与 fsrs-rs `power_forgetting_curve_scalar` 的
`t.max(0.0).round()` 一致，`.5` 远离零），即曲线按整天计算。
**定义性检验：`t == S` 时 `R` 恰好为 0.9**——这是稳定性定义的直接推论，
测试里对 S=1/10/365 都做了断言。

### 稳定性

```
首次复习（播种）：  S0 = clamp(w[G-1], S_MIN, S_MAX)

成功（G>=2，跨天）：S' = S · (1 + e^{w8} · (11-D) · S^{-w9} · (e^{w10(1-R)} - 1) · h · b)
                     h = w[15] 若 G=2（Hard 惩罚），否则 1
                     b = w[16] 若 G=4（Easy 奖励），否则 1
                     clamp 到 [S_MIN, S_MAX]

失败（G=1）：      s_fail = w[11] · D^{-w12} · ((S+1)^{w13} - 1) · e^{w14(1-R)}
                     floor  = S / e^{w17·w18}
                     S'     = max(min(s_fail, floor), S_MIN)

同日复习（t=0）：  sinc = S^{-w19} · e^{w17(G-3+w18)}
                     S'   = S · (G>=2 ? max(sinc, 1) : sinc)     // 成功不下降
```

成功路径的公式还可用 fsrs-rs 的 `memory_state_from_sm2` 反解来交叉印证：
该函数从 SM-2 的 ease factor 反推难度时用的正是
`D = 11 - (ef-1) / (e^{w8} · S^{-w9} · (e^{w10(1-R)} - 1))`，即上式的代数变形。

### 难度

```
首次：D0(G) = clamp(w[4] - e^{w5(G-1)} + 1, 1, 10)     // G=1 时恰为 w[4]
更新：ΔD  = -w[6] · (G-3)
      D'  = D + ΔD · (10-D)/9                            // 线性阻尼
      D'' = w[7] · D0(Easy) + (1 - w[7]) · D'            // 均值回归
      D_new = clamp(D'', 1, 10)
```

默认参数下 `D0` 依次为 6.4133 / 5.1122 / 2.1181 / 1.0。
注意 `D0(Easy)` 的解析值 `6.4133 - e^{0.8334·3} + 1 ≈ -4.77` 会被夹到 1.0；
这是公式在该组默认参数下的真实结果，不是 bug。

### 间隔

```
I(S, DR) = (S / factor) · (DR^{1/decay} - 1)
interval = clamp(round(I), 1, S_MAX)
```

`DR=0.9` 时 `I ≡ S`（测试断言 `I(S=10, 0.9) == 10`、`I(S=100, 0.9) == 100`）。
`I` 非有限（极端参数下 `pow()` 上溢）按 `S_MAX` 处理；上界 `S_MAX` 与
fsrs-rs `next_interval_scalar` 的 `clamp(0.0, S_MAX)` 对齐，同时保证结果恒落在
`uint32` 可表示范围内，杜绝整数转换溢出。
此处只返回按 `round` 取整的天数，**不含 Anki 的 fuzz**——模糊化属于调度外壳，
交给上层按自己的策略实现。

### 分支与调用顺序

```
current == NULL 或 stability <= 0  →  首次复习：直接播种 S0/D0，不经过遗忘曲线
                                      （首次 Again 不是 lapse，这点容易搞错）
t == 0                             →  同日（短期）模型
t  > 0 且 G == 1                   →  失败路径
t  > 0 且 G >= 2                   →  成功路径
```

难度在每个分支都会更新。每次推进后统一 clamp：`S ∈ [S_MIN, S_MAX]`、`D ∈ [1,10]`。
入口处输入 `stability` 先夹到 `[S_MIN, S_MAX]`（与 fsrs-rs `step` 入口一致），
失败路径出口同样带 `S_MAX` 上界；输入 `difficulty` 越出 `[1,10]` 视为非法状态，
返回 `FSRS_ERR_NUMERIC`（见 §7.7）。

---

## 3. 参数索引语义

| 索引 | 默认 | 作用 |
|---:|---:|---|
| 0–3 | 0.212 / 1.2931 / 2.3065 / 8.2956 | 首次复习后各评分的初始稳定性 |
| 4 | 6.4133 | 初始难度基础项（`= D0(Again)`） |
| 5 | 0.8334 | 初始难度的评分指数系数 |
| 6 | 3.0194 | 难度更新速率 |
| 7 | 0.001 | 均值回归权重（拉向 `D0(Easy)`） |
| 8–10 | 1.8722 / 0.1666 / 0.796 | 成功路径：增益底数 / 饱和指数 / 可提取性影响 |
| 11–14 | 1.4835 / 0.0614 / 0.2629 / 1.6483 | 失败路径：难度因子 / 难度幂 / 增长幂 / 可提取性影响 |
| 15 | 0.6014 | Hard 惩罚（仅 G=2） |
| 16 | 1.8729 | Easy 奖励（仅 G=4） |
| 17–19 | 0.5425 / 0.0912 / 0.0658 | 短期：评分指数 / 评分偏移 / 幂律指数 |
| 20 | 0.1542 | 遗忘曲线衰减（正值，decay 取其负） |

---

## 4. API

### 无状态内核（无分配、线程安全）

| 函数 | 说明 |
|---|---|
| `fsrs_default_parameters()` | 21 个默认权重（静态存储） |
| `fsrs_retrievability(w, S, t)` | 可提取性 R |
| `fsrs_next_interval(w, S, DR)` | 下一间隔（天），DR 非法返回 0，结果夹到 [1, 36500] |
| `fsrs_init_stability / fsrs_init_difficulty(w, G)` | 首次复习的 S0 / D0 |
| `fsrs_next_difficulty(w, D, G)` | 难度更新 |
| `fsrs_next_states(w, DR, current, days, out)` | 四个评分的后继状态 + 间隔 |
| `fsrs_next_states_pick(states, G)` | 取对应评分的状态 |
| `fsrs_next_states_interval(states, G)` | 取对应评分的间隔 |
| `fsrs_memory_state(w, reviews, n, out)` | 复习历史回放（新卡起步） |
| `fsrs_memory_state_from(w, start, reviews, n, out)` | 从起始态回放（截断历史） |

`w` 一律可为 `NULL`，表示使用默认参数；非 `NULL` 时必须指向 21 个**有限**值，
返回 `int` 的接口以 `FSRS_ERR_NUMERIC` 拒绝含 NaN/Inf 的 `w`。
状态类接口仅在返回 `FSRS_OK` 时写入 `out`，出错时 `out` 保持不变。

### FFI 层（opaque handle）

命名遵循 rs-fsrs-c 的规则 `fsrs_<struct>_<method>`：

```c
struct fsrs_scheduler *fsrs_scheduler_new(const float *w, size_t n, float dr, int *err);
void                   fsrs_scheduler_free(struct fsrs_scheduler *);
const float           *fsrs_scheduler_parameters(const struct fsrs_scheduler *);
float                  fsrs_scheduler_desired_retention(const struct fsrs_scheduler *);
int                    fsrs_scheduler_set_desired_retention(struct fsrs_scheduler *, float);
int                    fsrs_scheduler_next_states(const struct fsrs_scheduler *,
                                                  const struct fsrs_memory_state *,
                                                  uint32_t days, struct fsrs_next_states *);
int                    fsrs_scheduler_memory_state(const struct fsrs_scheduler *,
                                                   const struct fsrs_review *, size_t,
                                                   struct fsrs_memory_state *);
int                    fsrs_scheduler_memory_state_from(const struct fsrs_scheduler *,
                                                        const struct fsrs_memory_state *,
                                                        const struct fsrs_review *, size_t,
                                                        struct fsrs_memory_state *);
float                  fsrs_scheduler_retrievability(const struct fsrs_scheduler *,
                                                     const struct fsrs_memory_state *, uint32_t);
uint32_t               fsrs_scheduler_next_interval(const struct fsrs_scheduler *, float);
```

所有权约定：`new` 返回的 handle 归调用方，只能 `free` 一次；`free(NULL)` 安全；
`parameters` 返回的数组随 handle 生命周期有效，**不可由外部释放**。
handle 本身只读共享是线程安全的（内部无可变状态），
`set_desired_retention` 需要外部串行化。

错误码一律用非负整数返回，不使用 `errno`、不把错误混进 NaN：
`FSRS_OK` / `ERR_NULL` / `ERR_PARAM_COUNT` / `ERR_RATING` / `ERR_RETENTION` /
`ERR_NUMERIC` / `ERR_ALLOC`。
状态类接口只在 `FSRS_OK` 时写入出参，出错时出参保持不变。

### 与 fsrs-rs 的类型对应

| fsrs-rs (Rust) | 本实现 |
|---|---|
| `FSRS::new(Option<&[f32]>)` | `fsrs_scheduler_new(w, n, dr, &err)` |
| `MemoryState { stability, difficulty, stability_fast }` | `struct fsrs_memory_state`（字段顺序一致，`repr(C)`） |
| `next_states(Option<MemoryState>, dr, days)` → `NextStates` | `fsrs_next_states(w, dr, current, days, out)` |
| `NextStates { again, hard, good, easy }` | `struct fsrs_next_states`（含四个 interval） |
| `memory_state(FSRSItem, None)` | `fsrs_memory_state(w, reviews, n, out)` |
| `memory_state(FSRSItem, Some(start))` | `fsrs_memory_state_from(w, start, reviews, n, out)` |
| `current_retrievability(state, days)` | `fsrs_retrievability(w, S, t)` |
| `next_interval(S, dr)` → `u32` | `fsrs_next_interval(w, S, dr)` |
| `FSRSReview { rating, delta_t }` | `struct fsrs_review { rating, delta_days }` |

---

## 5. 验证

`make test`（144 项断言，全部通过）覆盖：

- **定义性检验**：`R(t=S) == 0.9`（S=1/10/365）、`I(S, 0.9) == S`（S=10/100）
- **参考向量**：与独立 Python 实现一致的初值、四个评分的转移结果、同日分支、历史回放
- **单调性**：`S_easy > S_good > S_hard > S_again`、`D_again > D_hard > D_good > D_easy`
- **不变量**：400 次连续推进后 `S >= S_MIN`、`D ∈ [1,10]`、无 NaN/Inf
- **边界**：`S=S_MIN`、`D=10`、`t=36500`；`S=S_MAX` + 小 `DR` 的间隔上界、
  退化 `w[20]` 的 `pow()` 上溢；`DR` 非法、`rating` 越界、`w` 长度错误、
  `w` 含 NaN/Inf、NaN 状态、输入 `S` 越界、输入 `D` 越界
- **对齐与契约**：R 曲线整天取整（含 `.5` 远离零、负值归 0）、截断历史从起始态
  回放与完整回放一致、出错时 `*out` 保持不变
- **FFI 契约**：`free(NULL)` 安全、重复释放之外的所有权规则、错误码路径

`make crosscheck` 把 C 内核与 `scripts/reference.py`（**独立书写、同源于公式**的
Python 实现）在 1124 组状态转移上逐项比对
（`S ∈ {0.01…36500} × D ∈ {1…10} × t ∈ {0…3650} × 4 个评分`，含新卡）：
最大相对误差 **1.1e-07**（float 存储精度量级），间隔 **0 处不一致**。

---

## 6. 精度与数值处理

- 所有中间运算用 `double`，只在写入结构体时降为 `float`。
  这与 fsrs-rs 一致（其遗忘曲线即以 f64 计算后转 f32），可显著降低累乘漂移。
- 除零/负数保护：`R` 的底数 `1 + factor·t/S` 不大于 0 时返回 0；`factor` 为 0 时
  间隔退化为 1；`S` 参与除法前先抬到 `S_MIN`。
- `pow()` 的负底数、`exp()` 的上溢都在进入前被夹住，不会产出 NaN。
- 间隔计算先判 `isfinite` 再夹到 `[1, S_MAX]`（与 fsrs-rs `next_interval_scalar`
  的 `clamp(0.0, S_MAX)` 对齐），杜绝 `double → uint32` 转换溢出的未定义行为。
- 创建调度器与 `fsrs_next_states` 入口全量校验 21 个参数有限
  （上游 `check_and_fill_parameters` 同样全量校验），且 `fsrs_step` 的所有分支
  （含新卡播种）都对输出做有限性验收：**`FSRS_OK` 永不伴随 NaN/Inf 状态**。

---

## 7. 已知差异与设计选择

以下几点在不同实现间存在分歧，这里明确本库的选择，便于针对性核对：

1. **`S_MIN` 取 0.01**（fsrs-rs 1.2.3 的值）。py-fsrs 使用 0.001。
   只影响失败后稳定性的地板，若需严格对齐 py-fsrs，改 `FSRS_S_MIN` 即可。
2. **均值回归的目标 `D0(Easy)` 用 clamp 后的值（1.0）**。
   ts-fsrs 传入的是未 clamp 的解析值 `-4.77`。由于权重 `w[7]=0.001`，
   两者差异小于 6e-6，可忽略；如需逐位复现 ts-fsrs，把
   `fsrs_internal_next_difficulty` 里的 clamp 去掉即可。
3. **短期分支的触发条件是 `elapsed_days == 0`**（ts-fsrs 与 fsrs-rs 的
   整数天数语义）。`srs-benchmark` 用 `delta_t < 1`；若上层用浮点天数且有
   亚天级间隔，需要自行改判 `< 1`。
4. **失败后的地板用 `S / e^{w17·w18}`**（`srs-benchmark` 的 FSRS-6 写法）。
   部分 FSRS-5 时代的实现（含一些第三方移植）直接用 `min(s_fail, S)`。
   FSRS-6 的短期因子让地板略低于原稳定性（默认参数下约为 `0.9517·S`）。
5. **`stability_fast` 恒等于 `stability`**：FSRS-6 的所有分支（含同日）都是如此，
   与 fsrs-rs 的 v6 路径（`stability_fast: stability`）一致。该字段只为与
   `MemoryState` 的字段布局（以及 FSRS-7 的未来扩展）保持一致而存在，
   不参与任何状态推进或间隔计算。
6. **不含 fuzz（间隔模糊化）与学习步骤（learning steps）**。这两者是 Anki 调度
   外壳的策略，不是记忆模型的一部分；`interval` 返回的是按 `round` 取整的模型值。
7. **输入 `difficulty` 越出 [1,10] 返回 `FSRS_ERR_NUMERIC`**，而上游 fsrs-rs 在
   `step` 入口直接 `clamp(D_MIN, D_MAX)` 后照算。本库把越界难度当作非法状态显式
   报错而不是静默修正；`stability` 则与上游一致地在入口 clamp 到 `[S_MIN, S_MAX]`。
8. **`next_states` 对 `stability <= 0` 的退化输入按新卡播种**。fsrs-rs 的
   `next_states` 对 `Some(state)` 固定传 `nth=1`，不播种，而是把 S 夹到 `S_MIN`
   照常计算（其播种条件 `nth == 0 && stability == 0` 只在历史起点生效）。
   历史回放路径上两者等价；本库统一按新卡处理，是为了让全零状态（如空回放的
   输出）能自然走首次复习路径。
9. **不做参数裁剪（clip）**。fsrs-rs 的 `FSRS::new` 会跑 `clip_parameters` 把权重
   裁回训练时的合法范围；本库只校验 21 个值有限，其余完全交给调用方，便于接入
   任何优化器的结果——但也意味着极端（有限的）权重不会被自动修正。

---

## 8. 文件结构

```
include/fsrs.h            公共 ABI（唯一需要 #include 的头文件）
src/fsrs_internal.h       内核内部声明
src/fsrs_core.c           FSRS-6 算法内核（遗忘曲线 / S / D / 间隔 / 回放）
src/fsrs_ffi.c            opaque scheduler（create/destroy、参数复制、错误翻译）
tests/test_fsrs.c         144 项自测
tests/crosscheck.c        输出状态转移 CSV 供比对
scripts/reference.py      独立 Python 参考实现
scripts/crosscheck.py     C vs Python 比对脚本
examples/schedule.c       调度示例
Makefile                  构建 / 测试 / 交叉验证 / 共享库
```

## 9. 公式来源

- `fsrs-rs/src/inference_v6.rs` —— `FSRS6_DEFAULT_PARAMETERS`（21 参数）、`FSRS6_DEFAULT_DECAY`
- `srs-benchmark/models/fsrs_v6.py` —— 遗忘曲线、短期稳定性、失败稳定性
- `ts-fsrs/packages/fsrs/src/algorithm.ts` —— 初始难度、难度更新（线性阻尼 + 均值回归）、成功稳定性
- `docs.rs/fsrs/1.2.3` 的 `inference.rs` —— `S_MIN`、`next_interval`、`memory_state_from_sm2`（成功路径公式的反解印证）
- `rs-fsrs-c` README —— FFI 命名与内存管理约定
