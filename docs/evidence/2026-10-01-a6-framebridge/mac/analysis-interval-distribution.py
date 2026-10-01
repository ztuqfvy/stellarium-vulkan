import csv, gzip, statistics, sys

STEADY_START = 1500.0
THR_ABS = 67.98   # 本轮门槛（用于跨跑统一比较）

def pct(vals, p):
    v = sorted(vals)
    k = (len(v)-1) * p/100.0
    lo = int(k); hi = min(lo+1, len(v)-1)
    return v[lo] + (v[hi]-v[lo])*(k-lo)

def load(path):
    op = gzip.open if path.endswith(".gz") else open
    with op(path, "rt", encoding="utf-8-sig") as f:
        return [(float(d["t_s"]), d["phase"], float(d["interval_ms"])) for d in csv.DictReader(f)]

def report(name, path):
    rows = load(path)
    m = [(t, iv) for (t, ph, iv) in rows if ph == "measure"]
    st = [iv for (t, iv) in m if t >= STEADY_START]
    n = len(st)
    mean = statistics.mean(st)
    fps = 1000.0/mean
    thr3 = 3*mean
    print("%-24s n=%6d  fps=%6.2f  mean=%6.2f  3x门槛=%6.2f" % (name, n, fps, mean, thr3))
    print("    p50=%6.2f  p90=%6.2f  p95=%6.2f  p99=%6.2f  p99.9=%7.2f  max=%8.2f" % (
        pct(st,50), pct(st,90), pct(st,95), pct(st,99), pct(st,99.9), max(st)))
    # 归一化（÷mean）
    print("    归一化 p90=%.2fx p95=%.2fx p99=%.2fx  | 占自身门槛: p99/3x=%.1f%%  | max/6x=%.2fx" % (
        pct(st,90)/mean, pct(st,95)/mean, pct(st,99)/mean, pct(st,99)/thr3, max(st)/(6*mean)))
    # 越界占比（各自门槛 3×mean；同时给统一 68ms）
    over_own = sum(1 for x in st if x > thr3)
    over_abs = sum(1 for x in st if x > THR_ABS)
    print("    >自身3x门槛: %5d (%.3f%%)   >68ms(统一): %5d (%.3f%%)" % (
        over_own, 100*over_own/n, over_abs, 100*over_abs/n))
    print("    各档位计数: 1x[%.1f,%.1f)=%d  2x=%d  3x=%d  4-6x=%d  >6x=%d" % (
        mean*0.5, mean*1.5,
        sum(1 for x in st if 0.5*mean <= x < 1.5*mean),
        sum(1 for x in st if 1.5*mean <= x < 2.5*mean),
        sum(1 for x in st if 2.5*mean <= x < 3.5*mean),
        sum(1 for x in st if 3.5*mean <= x < 6.5*mean),
        sum(1 for x in st if x >= 6.5*mean)))
    print()

E = "/Users/ztuqfvy/qt_demo/stellarium_vulkan/docs/evidence/2026-09-23-t13-live-longrun/"
report("T13-run1(FAIL,max)", E+"17-official-run1.frames.csv.gz")
report("T13-run2(PASS)",      E+"18-official-run2.frames.csv.gz")
report("本轮 A6",             "/tmp/a6-longrun/full.frames.csv")
