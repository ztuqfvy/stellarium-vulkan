# 帧间隔分布同口径对照：T13 两跑 vs 本跑
# 用法: python3 a6-cmp-dist.py <本跑.frames.csv> [标签]
import csv, gzip, statistics, sys

STEADY_START = 1500.0   # 稳态窗起点（warm900 + measure/3）

def pct(vals, p):
    v = sorted(vals)
    k = (len(v)-1) * p/100.0
    lo = int(k); hi = min(lo+1, len(v)-1)
    return v[lo] + (v[hi]-v[lo])*(k-lo)

def load(path):
    op = gzip.open if path.endswith(".gz") else open
    with op(path, "rt", encoding="utf-8-sig") as f:
        return [(float(d["t_s"]), d["phase"], float(d["interval_ms"])) for d in csv.DictReader(f)]

def report(name, path, steady_start=STEADY_START):
    try:
        rows = load(path)
    except Exception as e:
        print("%-24s 读取失败: %s" % (name, e)); return
    m = [(t, iv) for (t, ph, iv) in rows if ph == "measure"]
    st = [iv for (t, iv) in m if t >= steady_start]
    n = len(st)
    if n < 100:
        print("%-24s 稳态样本不足 n=%d" % (name, n)); return
    mean = statistics.mean(st); fps = 1000.0/mean; thr3 = 3*mean
    print("%-26s n=%6d  fps=%6.2f  mean=%6.2f  3x门槛=%6.2f" % (name, n, fps, mean, thr3))
    print("    p50=%6.2f  p90=%6.2f  p95=%6.2f  p99=%6.2f  p99.9=%7.2f  max=%8.2f" % (
        pct(st,50), pct(st,90), pct(st,95), pct(st,99), pct(st,99.9), max(st)))
    print("    归一化 p90=%.2fx p95=%.2fx p99=%.2fx | p99/自身3x门槛=%.1f%% | max/6x=%.2fx" % (
        pct(st,90)/mean, pct(st,95)/mean, pct(st,99)/mean, pct(st,99)/thr3, max(st)/(6*mean)))
    o3  = sum(1 for x in st if 2.5*mean <= x < 3.5*mean)
    o46 = sum(1 for x in st if 3.5*mean <= x < 6.5*mean)
    o6  = sum(1 for x in st if x >= 6.5*mean)
    print("    档位: 1x=%d  2x=%d  3x=%d  4-6x=%d  >6x=%d   || >自身3x门槛=%d (%.3f%%)" % (
        sum(1 for x in st if 0.5*mean <= x < 1.5*mean),
        sum(1 for x in st if 1.5*mean <= x < 2.5*mean),
        o3, o46, o6, sum(1 for x in st if x > thr3), 100*sum(1 for x in st if x > thr3)/n))
    print("    判定: p99 %s 门槛；max 档占比 %.4f%% %s 0.01%%" % (
        "超" if pct(st,99) > thr3 else "未超",
        100*o6/n, "超" if 100*o6/n > 0.01 else "未超"))
    print()

E = "/Users/ztuqfvy/qt_demo/stellarium_vulkan/docs/evidence/2026-09-23-t13-live-longrun/"
report("T13-run1(FAIL,max)", E+"17-official-run1.frames.csv.gz")
report("T13-run2(PASS)",      E+"18-official-run2.frames.csv.gz")
if len(sys.argv) > 1:
    report(sys.argv[2] if len(sys.argv) > 2 else "本次跑", sys.argv[1])
