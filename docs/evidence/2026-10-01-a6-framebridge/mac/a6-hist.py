import csv, gzip, statistics, sys

STEADY = 1500.0
E = "/Users/ztuqfvy/qt_demo/stellarium_vulkan/docs/evidence/2026-09-23-t13-live-longrun/"

def load(path):
    op = gzip.open if path.endswith(".gz") else open
    with op(path, "rt", encoding="utf-8-sig") as f:
        return [(float(d["t_s"]), d["phase"], int(d["frame_number"]), float(d["interval_ms"]))
                for d in csv.DictReader(f)]

def steady(rows):
    return [(fn, iv) for (t, ph, fn, iv) in rows if ph == "measure" and t >= STEADY]

def hist(name, path):
    st = steady(load(path))
    ivs = [iv for _, iv in st]
    mean = statistics.mean(ivs)
    print("### %s   n=%d  mean=%.2fms  fps=%.2f" % (name, len(st), mean, 1000/mean))
    bins = [(0,10),(10,20),(20,30),(30,40),(40,50),(50,60),(60,70),(70,80),(80,90),(90,100),(100,120),(120,150),(150,999)]
    print("    %-12s %8s %8s %8s" % ("区间(ms)", "计数", "占比%", "÷mean"))
    for lo, hi in bins:
        c = sum(1 for x in ivs if lo <= x < hi)
        tag = "%d-%d" % (lo, hi) if hi < 999 else "%d+" % lo
        print("    %-12s %8d %7.3f%% %7.2fx" % (tag, c, 100*c/len(ivs), lo/mean))
    # 簇分析：把"慢帧"(>=2.5x mean)标出，看连续性
    slow = [i for i, (fn, iv) in enumerate(st) if iv >= 2.5*mean]
    clusters, run = [], 1
    for a, b in zip(slow, slow[1:]):
        if b == a+1: run += 1
        else: clusters.append(run); run = 1
    if slow: clusters.append(run)
    print("    慢帧(>=2.5x) 计数=%d（占 %.3f%%）；簇数=%d" % (len(slow), 100*len(slow)/len(st), len(clusters)))
    if clusters:
        from collections import Counter
        cc = Counter(clusters)
        print("    簇长度分布: %s" % dict(sorted(cc.items())[:12]))
        print("    单帧簇占比=%.1f%%（若 >70%% 说明是'散点'而非'成段停顿'）" % (100*cc.get(1,0)/len(clusters)))
    print()

hist("T13-run2(PASS)", E+"18-official-run2.frames.csv.gz")
hist("A6-干净跑",       "/tmp/a6-longrun2/full.frames.csv")
hist("A6-污染跑",       "/tmp/a6-longrun/full.frames.csv")
