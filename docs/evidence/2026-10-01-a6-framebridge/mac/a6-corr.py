# 把干净跑的逐秒 CSV 与"该秒内的慢帧率"关联
import csv, statistics
from collections import defaultdict

F = "/tmp/a6-longrun2/full.frames.csv"
C = "/tmp/a6-longrun2/full.csv"
STEADY = 1500.0

per_frame = defaultdict(list)          # 秒(整) -> [interval]
with open(F, encoding="utf-8-sig") as f:
    for d in csv.DictReader(f):
        if d["phase"] == "measure":
            t = float(d["t_s"])
            if t >= STEADY:
                per_frame[int(t)].append(float(d["interval_ms"]))

per_sec = {}
with open(C, encoding="utf-8-sig") as f:
    for d in csv.DictReader(f):
        if d["phase"] == "measure":
            t = float(d["t_s"])
            if t >= STEADY:
                per_sec[int(t)] = d

# 每秒的慢帧率（>=2.5x 全局均值）
all_iv = [x for v in per_frame.values() for x in v]
mean = statistics.mean(all_iv); thr = 2.5*mean
sec_slow = {s: (sum(1 for x in v if x >= thr), len(v)) for s, v in per_frame.items()}

# 高慢帧秒 vs 低慢帧秒 的环境字段对比
hi = [s for s,(c,n) in sec_slow.items() if c >= 3 and n >= 30]
lo = [s for s,(c,n) in sec_slow.items() if c == 0 and n >= 30]
print("慢帧密集秒(≥3帧/秒)=%d  零慢帧秒=%d" % (len(hi), len(lo)))

def field(name, conv=float):
    try:
        a = [conv(per_sec[s][name]) for s in hi if s in per_sec]
        b = [conv(per_sec[s][name]) for s in lo if s in per_sec]
        if a and b:
            print("  %-18s 密集秒 mean=%-14s 零慢帧秒 mean=%-14s" % (name,
                "%.2f" % statistics.mean(a), "%.2f" % statistics.mean(b)))
    except (KeyError, ValueError):
        pass

for f in ("mailbox_age_ms","upload_count","upload_max_ms","rss_kb","footprint_kb",
          "producer_fps","producer_rendered","degraded","exposed"):
    field(f)
# 密集秒的连续性：密集秒是否相邻出现（连续段 => 成段外因；孤立 => 散点）
hi_set = set(hi)
runs, run = [], 1
for a, b in zip(hi, hi[1:]):
    if b == a+1: run += 1
    else: runs.append(run); run = 1
if hi: runs.append(run)
from collections import Counter
print("密集秒连续段长度分布: %s" % dict(sorted(Counter(runs).items())[:8]))
