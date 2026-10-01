# 段内相关性：产能（fps）与 p99 归一化的关系
import csv, statistics

F = "/tmp/a6-longrun2/full.frames.csv"
STEADY = 1500.0; SEG = 120.0

rows = []
with open(F, encoding="utf-8-sig") as f:
    for d in csv.DictReader(f):
        if d["phase"] == "measure":
            t = float(d["t_s"])
            if t >= STEADY:
                rows.append((t, float(d["interval_ms"])))

def pct(v, p):
    v = sorted(v); k = (len(v)-1)*p/100.0
    lo = int(k); hi = min(lo+1, len(v)-1)
    return v[lo] + (v[hi]-v[lo])*(k-lo)

from collections import defaultdict
segs = defaultdict(list)
for t, iv in rows:
    segs[int((t-STEADY)//SEG)].append(iv)

print("%-8s %7s %8s %8s %8s %8s %9s" % ("段","fps(mean)","p50","p95","p99","p99/mean","3x档计数"))
xs, ys = [], []
for s in sorted(segs):
    v = segs[s]
    m = statistics.mean(v); fps = 1000/m
    p99 = pct(v, 99)
    c3 = sum(1 for x in v if 2.5*m <= x < 3.5*m)
    print("%-8d %7.2f %8.2f %8.2f %8.2f %8.2f %9d" % (s, fps, pct(v,50), pct(v,95), p99, p99/m, c3))
    xs.append(fps); ys.append(p99/m)

# Pearson 相关
n = len(xs)
mx, my = statistics.mean(xs), statistics.mean(ys)
cov = sum((a-mx)*(b-my) for a,b in zip(xs,ys)) / n
sx = statistics.pstdev(xs); sy = statistics.pstdev(ys)
r = cov/(sx*sy) if sx and sy else float('nan')
print("\n产能 fps 与 p99 归一化的 Pearson r = %.3f（n=%d 段）" % (r, n))
