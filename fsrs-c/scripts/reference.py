#!/usr/bin/env python3
"""
FSRS-6 独立参考实现（用于交叉验证 C 内核）。

公式来源与 C 实现同源，但完全独立书写，用于数值对照：
  srs-benchmark/models/fsrs_v6.py、ts-fsrs/packages/fsrs/src/algorithm.ts、
  fsrs-rs/src/inference_v6.rs（参数表）。

用法：
    python3 scripts/reference.py            # 打印关键向量
    python3 scripts/reference.py --json     # 输出 JSON，供 C 测试比对
"""
import json
import math
import sys

W = [
    0.212, 1.2931, 2.3065, 8.2956,          # w0-w3  初始稳定性
    6.4133, 0.8334, 3.0194, 0.001,          # w4-w7  难度
    1.8722, 0.1666, 0.796,                  # w8-w10 成功路径
    1.4835, 0.0614, 0.2629, 1.6483,         # w11-w14 失败路径
    0.6014, 1.8729,                         # w15-w16 Hard 惩罚 / Easy 奖励
    0.5425, 0.0912, 0.0658,                 # w17-w19 短期
    0.1542,                                 # w20    衰减
]

S_MIN, S_MAX = 0.01, 36500.0
D_MIN, D_MAX = 1.0, 10.0
LN_09 = math.log(0.9)

AGAIN, HARD, GOOD, EASY = 1, 2, 3, 4


def clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def round_half_away(x):
    """与 C 的 round() / Rust 的 f32::round 一致：就近取整，.5 远离零。"""
    return math.floor(x + 0.5) if x >= 0 else math.ceil(x - 0.5)


def factor(w=W):
    decay = -w[20]
    e = min(LN_09 / decay, 60.0)
    return math.exp(e) - 1.0


def retrievability(stability, elapsed_days, w=W):
    decay = -w[20]
    f = factor(w)
    s = max(stability, S_MIN)
    # 曲线按整天计算：与 fsrs-rs 的 t.max(0.0).round() 一致（NaN 经 max 归 0）
    t = round_half_away(max(0.0, elapsed_days))
    r = (1.0 + f * t / s) ** decay
    return clamp(r, 0.0, 1.0)


def next_interval(stability, desired_retention, w=W):
    decay = -w[20]
    f = factor(w)
    s = max(stability, S_MIN)
    if f == 0.0:
        return 1
    i = s / f * (desired_retention ** (1.0 / decay) - 1.0)
    if not math.isfinite(i):
        return int(S_MAX)
    # 上界与 fsrs-rs 的 next_interval_scalar 的 clamp(0.0, S_MAX) 一致
    return int(clamp(round_half_away(i), 1, S_MAX))


def init_stability(rating, w=W):
    return clamp(w[rating - 1], S_MIN, S_MAX)


def init_difficulty_raw(rating, w=W):
    return w[4] - math.exp(w[5] * (rating - 1)) + 1.0


def init_difficulty(rating, w=W):
    return clamp(init_difficulty_raw(rating, w), D_MIN, D_MAX)


def next_difficulty(d, rating, w=W):
    delta_d = -w[6] * (rating - 3)
    damped = d + delta_d * (10.0 - d) / 9.0
    reverted = w[7] * clamp(init_difficulty_raw(EASY, w), D_MIN, D_MAX) \
        + (1.0 - w[7]) * damped
    return clamp(reverted, D_MIN, D_MAX)


def stability_after_success(d, s, r, rating, w=W):
    h = w[15] if rating == HARD else 1.0
    b = w[16] if rating == EASY else 1.0
    alpha = (1.0 + math.exp(w[8]) * (11.0 - d) * s ** (-w[9])
             * (math.exp((1.0 - r) * w[10]) - 1.0) * h * b)
    return clamp(s * alpha, S_MIN, S_MAX)


def stability_after_failure(d, s, r, w=W):
    s_fail = w[11] * d ** (-w[12]) * ((s + 1.0) ** w[13] - 1.0) \
        * math.exp((1.0 - r) * w[14])
    floor = s / math.exp(w[17] * w[18])
    return max(min(s_fail, floor), S_MIN)


def stability_short_term(s, rating, w=W):
    sinc = s ** (-w[19]) * math.exp(w[17] * (rating - 3 + w[18]))
    masked = max(sinc, 1.0) if rating >= HARD else sinc
    return clamp(s * masked, S_MIN, S_MAX)


def step(state, elapsed_days, rating, w=W):
    """state 为 None 或 stability<=0 时按新卡处理。"""
    if state is None or state[0] <= 0:
        s = init_stability(rating, w)
        d = init_difficulty(rating, w)
        return (s, d, s)
    s, d, _ = state
    nd = next_difficulty(d, rating, w)
    if elapsed_days == 0:
        ns = stability_short_term(s, rating, w)
    else:
        r = retrievability(s, elapsed_days, w)
        if rating == AGAIN:
            ns = stability_after_failure(d, s, r, w)
        else:
            ns = stability_after_success(d, s, r, rating, w)
    return (ns, nd, ns)


def next_states(state, desired_retention, elapsed_days, w=W):
    out = {}
    for rating in (AGAIN, HARD, GOOD, EASY):
        ns, nd, nf = step(state, elapsed_days, rating, w)
        out[rating] = {
            "stability": ns,
            "difficulty": nd,
            "stability_fast": nf,
            "interval": next_interval(ns, desired_retention, w),
        }
    return out


def memory_state(reviews, w=W):
    """reviews: [(rating, delta_days), ...]"""
    state = None
    for rating, delta_days in reviews:
        state = step(state, delta_days, rating, w)
    return state


def vectors():
    """一组关键参考向量，供 C 侧逐项比对。"""
    v = {}
    v["factor"] = factor()
    v["init_stability"] = {g: init_stability(g) for g in (1, 2, 3, 4)}
    v["init_difficulty"] = {g: init_difficulty(g) for g in (1, 2, 3, 4)}
    # R 的定义性检查：t == S 时 R 应为 0.9
    v["retrievability_at_S"] = retrievability(10.0, 10.0)
    v["retrievability_3d_S10"] = retrievability(10.0, 3.0)
    v["interval_at_dr09_S10"] = next_interval(10.0, 0.9)
    v["interval_at_dr085_S10"] = next_interval(10.0, 0.85)
    v["new_card_good"] = step(None, 0, GOOD)
    v["new_card_again"] = step(None, 0, AGAIN)
    v["new_card_easy"] = step(None, 0, EASY)
    # 复习卡：S=10, D=5，间隔 10 天后 Good
    v["review_good_t10"] = step((10.0, 5.0, 10.0), 10, GOOD)
    v["review_hard_t10"] = step((10.0, 5.0, 10.0), 10, HARD)
    v["review_easy_t10"] = step((10.0, 5.0, 10.0), 10, EASY)
    v["review_again_t10"] = step((10.0, 5.0, 10.0), 10, AGAIN)
    # 同日复习（短期分支）
    v["same_day_good"] = step((10.0, 5.0, 10.0), 0, GOOD)
    v["same_day_again"] = step((10.0, 5.0, 10.0), 0, AGAIN)
    # 历史回放
    v["replay"] = memory_state([(GOOD, 0), (GOOD, 1), (GOOD, 3), (GOOD, 7), (GOOD, 15)])
    v["next_states_S10_D5_t10"] = next_states((10.0, 5.0, 10.0), 0.9, 10)
    return v


if __name__ == "__main__":
    data = vectors()
    if "--json" in sys.argv:
        print(json.dumps(data, indent=2, sort_keys=True, default=str))
    else:
        print(f"factor                 = {data['factor']:.8f}")
        for g in (1, 2, 3, 4):
            print(f"init S/D (rating={g})    = {data['init_stability'][g]:.6f} / "
                  f"{data['init_difficulty'][g]:.6f}")
        print(f"R(t=S=10)              = {data['retrievability_at_S']:.8f}  (应为 0.9)")
        print(f"R(t=3, S=10)           = {data['retrievability_3d_S10']:.8f}")
        print(f"I(S=10, DR=0.9)        = {data['interval_at_dr09_S10']}  (应为 10)")
        print(f"I(S=10, DR=0.85)       = {data['interval_at_dr085_S10']}")
        print(f"new card Good          = S={data['new_card_good'][0]:.6f} "
              f"D={data['new_card_good'][1]:.6f}")
        print(f"review Good t=10       = S={data['review_good_t10'][0]:.6f} "
              f"D={data['review_good_t10'][1]:.6f}")
        print(f"review Again t=10      = S={data['review_again_t10'][0]:.6f} "
              f"D={data['review_again_t10'][1]:.6f}")
        print(f"same-day Good          = S={data['same_day_good'][0]:.6f}")
        print(f"replay 5x Good         = S={data['replay'][0]:.6f} "
              f"D={data['replay'][1]:.6f}")
