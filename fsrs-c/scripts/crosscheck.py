#!/usr/bin/env python3
"""
把 C 内核的输出与独立 Python 参考实现逐项比对。

用法：
    make crosscheck        # 内部会先跑 C 生成 CSV，再由本脚本比对
    # 或手动：
    ./build/crosscheck > /tmp/c.csv && python3 scripts/crosscheck.py /tmp/c.csv
"""
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference as R  # noqa: E402

REL_TOL = 1e-5   # 相对容差：C 内部用 double 计算但以 float 落盘
ABS_TOL = 1e-6


def rel_err(got, expect):
    denom = max(1.0, abs(expect))
    return abs(got - expect) / denom


def main(path):
    rows = 0
    worst_s = 0.0
    worst_d = 0.0
    interval_mismatch = 0
    failures = []

    with open(path) as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            rating = int(row["rating"])
            s_in = float(row["stability_in"])
            d_in = float(row["difficulty_in"])
            days = int(row["elapsed_days"])
            s_out = float(row["stability_out"])
            d_out = float(row["difficulty_out"])
            sf_out = float(row["stability_fast_out"])
            interval = int(row["interval"])

            state = None if s_in <= 0 else (s_in, d_in, s_in)
            exp_s, exp_d, exp_sf = R.step(state, days, rating)
            exp_interval = R.next_interval(exp_s, 0.9)

            es = rel_err(s_out, exp_s)
            ed = rel_err(d_out, exp_d)
            worst_s = max(worst_s, es)
            worst_d = max(worst_d, ed)

            rows += 1
            if es > REL_TOL and abs(s_out - exp_s) > ABS_TOL:
                failures.append(f"S mismatch rating={rating} S_in={s_in} D_in={d_in} "
                                f"t={days}: C={s_out!r} py={exp_s!r}")
            if ed > REL_TOL and abs(d_out - exp_d) > ABS_TOL:
                failures.append(f"D mismatch rating={rating} S_in={s_in} D_in={d_in} "
                                f"t={days}: C={d_out!r} py={exp_d!r}")
            if rel_err(sf_out, exp_sf) > REL_TOL:
                failures.append(f"S_fast mismatch rating={rating} S_in={s_in} t={days}: "
                                f"C={sf_out!r} py={exp_sf!r}")
            if interval != exp_interval:
                interval_mismatch += 1
                failures.append(f"interval mismatch rating={rating} S_in={s_in} "
                                f"D_in={d_in} t={days}: C={interval} py={exp_interval}")

    print(f"compared {rows} state transitions")
    print(f"max relative error: stability={worst_s:.3e} difficulty={worst_d:.3e}")
    print(f"interval mismatches: {interval_mismatch}")
    if failures:
        print(f"\n{len(failures)} FAILURES (showing first 10):")
        for f in failures[:10]:
            print("  " + f)
        return 1
    print("\nOK: C kernel matches the independent Python reference")
    return 0


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fsrs_crosscheck.csv"
    sys.exit(main(src))
