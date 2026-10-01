# 判定：慢帧是否周期性（间隔≈整数秒 => 周期性任务阻塞 GUI 线程）
import csv, gzip, statistics
from collections import Counter

STEADY = 1500.0
E = "/Users/ztuqfvy/qt_demo/stellarium_vulkan/docs/evidence/2026-09-23-t13-live-longrun/"

def load(path):
    op = gzip.open if path.endswith(".gz") else open
    with op(path, "rt", encoding="utf-8-sig") as f:
        return [(float(d["t_s"]), d["phase"], float(d["interval_ms"])) for d in csv.DictReader(f)]

def analyze(name, path, thr_mult=2.5):
    rows = [(t, iv) for (t, ph, iv) in load(path) if ph == "measure" and t >= STEADY]
    ivs = [iv for _, iv in rows]
    mean = statistics.mean(ivs)
    thr = thr_mult * mean
    slow = [(t, iv) for (t, iv) in rows if iv >= thr]
    print("### %s  mean=%.2fms  慢帧门槛=%.2fx=%.1fms  慢帧数=%d" % (name, mean, thr_mult, thr, len(slow)))
    if len(slow) < 5:
        print("    (样本太少)\n"); return
    # 相邻慢帧的时间差
    gaps = [b[0]-a[0] for a, b in zip(slow, slow[1:])]
    print("    相邻慢帧时间差: mean=%.3fs  p50=%.3fs  min=%.3fs  max=%.3fs" % (
        statistics.mean(gaps), sorted(gaps)[len(gaps)//2], min(gaps), max(gaps)))
    # 直方图（0.1s 桶，0-3s）
    bins = [(0,0.1),(0.1,0.3),(0.3,0.6),(0.6,0.9),(0.9,1.1),(1.1,1.5),(1.5,1.9),(1.9,2.1),(2.1,3.0),(3.0,999)]
    print("    %-12s %7s %7s" % ("间隔(s)","计数","占比%"))
    for lo, hi in bins:
        c = sum(1 for g in gaps if lo <= g < hi)
        tag = "%.1f-%.1f" % (lo,hi) if hi < 999 else "%.1f+" % lo
        print("    %-12s %7d %6.1f%%" % (tag, c, 100*c/len(gaps)))
    # 整数秒命中率
    for tol in (0.05, 0.10, 0.20):
        hit = sum(1 for g in gaps if abs(g-round(g)) <= tol and round(g) >= 1)
        print("    落在整数秒 ±%.2fs 内: %d / %d = %.1f%%" % (tol, hit, len(gaps), 100*hit/len(gaps)))
    print()

analyze("T13-run2(PASS)", E+"18-official-run2.frames.csv.gz")
analyze("A6-干净跑",       "/tmp/a6-longrun2/full.frames.csv")
