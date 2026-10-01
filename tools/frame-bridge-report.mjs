// A6 帧桥统计报告生成器（计划一 §7 契约第 6 条「分开的性能记录」的取证工具）。
//
// 为什么不手抄数字：A6 要求归档「平均/p95 帧年龄、读回/上传时间、队列长度、内存」。
// 手抄的数字无法随二进制重跑而更新，也无法被复核。本工具只做**读数的机械搬运**：
//   · 从长跑 stdout 解析 STELLRUN: 行（判据结论 + 不在 CSV 里的高频帧龄分布）
//   · 从逐秒 CSV 复算吞吐/上传/内存斜率/暴露率（可核对判据结论）
//   · 从逐帧 CSV 复算上屏间隔分位（mean/p50/p95/p99/max）
// 三者互相对照——若 CSV 复算与日志结论不一致，报告会显式报 ⚠ 不一致，绝不静默。
//
// 用法：
//   node tools/frame-bridge-report.mjs <longrun.log> [--csv A.csv.gz] [--frames B.csv.gz]
//                                      [--label "原 GL 基线"] [--json out.json]
// 路径可为 .csv 或 .csv.gz。

import fs from 'node:fs';
import zlib from 'node:zlib';
import path from 'node:path';

// ── 参数解析 ─────────────────────────────────────────────────────────────────
const argv = process.argv.slice(2);
if (argv.length === 0) {
  console.error('用法: node tools/frame-bridge-report.mjs <longrun.log> [--csv A.csv.gz] [--frames B.csv.gz] [--label NAME] [--json out.json]');
  process.exit(2);
}
const opts = { log: null, csv: null, frames: null, label: null, json: null };
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === '--csv') opts.csv = argv[++i];
  else if (a === '--frames') opts.frames = argv[++i];
  else if (a === '--label') opts.label = argv[++i];
  else if (a === '--json') opts.json = argv[++i];
  else if (!opts.log) opts.log = a;
  else throw new Error(`未知参数: ${a}`);
}

// ── 读文件（.gz 自动解）─────────────────────────────────────────────────────
function readMaybeGz(p) {
  const buf = fs.readFileSync(p);
  const data = (p.endsWith('.gz') || (buf[0] === 0x1f && buf[1] === 0x8b)) ? zlib.gunzipSync(buf) : buf;
  return data.toString('utf8');
}

// ── 分位（线性插值，与常见 p 分位口径一致）──────────────────────────────────
function quantile(sorted, q) {
  if (sorted.length === 0) return NaN;
  if (sorted.length === 1) return sorted[0];
  const pos = (sorted.length - 1) * q;
  const lo = Math.floor(pos), hi = Math.ceil(pos);
  if (lo === hi) return sorted[lo];
  return sorted[lo] + (sorted[hi] - sorted[lo]) * (pos - lo);
}
const fmt = (v, d = 2) => (Number.isFinite(v) ? v.toFixed(d) : '—');

// ── 解析 STELLRUN: 行 ───────────────────────────────────────────────────────
function parseLog(text) {
  const rec = { verdict: null, judge: [], info: [], header: null, notes: [] };
  for (const line of text.split('\n')) {
    const i = line.indexOf('STELLRUN:');
    if (i < 0) continue;
    const body = line.slice(i + 'STELLRUN:'.length).trim();
    let m;
    if ((m = body.match(/^VERDICT=(\S+)/))) rec.verdict = m[1];
    else if ((m = body.match(/^(SL-C\d+)\s+(PASS|FAIL|SKIP|INVALID)\s+(.*)$/))) {
      rec.judge.push({ id: m[1], pass: m[2], detail: m[3] });
    } else if ((m = body.match(/^(SL-INFO)\s+(.*)$/))) rec.info.push(m[2]);
    else if ((m = body.match(/^预热\s+(\d+)\s+秒开始（生产者名义\s+([\d.]+)\s+fps，(\S+)）/)))
      rec.header = { warmupSec: +m[1], nominalFps: +m[2], size: m[3] };
    else rec.notes.push(body);
  }
  return rec;
}

// 从判据详情里抽关键数字（判据输出的格式是稳定的，此处只抽该判据明示的量）
function pick(judge, id) {
  return judge.find((j) => j.id === id) || null;
}

// ── 解析逐秒 CSV（只取 measure 段）──────────────────────────────────────────
//
// ⚠ 两种窗口（对齐 SkyLongRun.cpp 的判据口径，不是本工具自创）：
//   · measure 全段   —— SL-C04/05/06/09/10 的口径（邮箱完整性/上传/暴露/降级）
//   · steady 稳态窗  —— SL-C01/02/03/07/11 的口径：**测量段的后 2/3**
//                        （steadyStartSec = warmup + measure/3，与 T9「最后 1200s」同源）
//   若混用，复算值会对不上日志（首版工具就栽在这：SL-C07 报 0.211 vs 日志 0.141）。
function parsePerSecond(text, steadyStartSec) {
  const lines = text.split('\n').filter((l) => l.trim().length > 0);
  const cols = lines[0].split(',');
  const idx = Object.fromEntries(cols.map((c, i) => [c.trim(), i]));
  const rows = [];
  for (let i = 1; i < lines.length; i++) {
    const f = lines[i].split(',');
    if (f.length < cols.length) continue;
    if (f[idx.phase].trim() !== 'measure') continue;
    rows.push({
      steady: +f[idx.t_s] >= steadyStartSec,
      t: +f[idx.t_s],
      displayed: +f[idx.displayed_frames],
      published: +f[idx.published],
      dropped: +f[idx.dropped],
      ageMs: +f[idx.mailbox_age_ms],
      uploadCount: +f[idx.upload_count],
      uploadSumUs: +f[idx.upload_sum_us],
      uploadMaxMs: +f[idx.upload_max_ms],
      rssKb: +f[idx.rss_kb],
      footprintKb: +f[idx.footprint_kb],
      exposed: f[idx.exposed].trim() === '1',
      degraded: f[idx.degraded].trim() === '1',
      producerFps: +f[idx.producer_fps],
      producerRendered: +f[idx.producer_rendered],
      producerFailed: +f[idx.producer_failed],
    });
  }
  if (rows.length === 0) return null;

  const toMiB = (kb) => kb / 1024;
  const slopeOf = (rs) => {
    if (rs.length < 2) return NaN;
    const mx = rs.reduce((a, r) => a + r.t, 0) / rs.length;
    const my = rs.reduce((a, r) => a + toMiB(r.footprintKb), 0) / rs.length;
    let num = 0, den = 0;
    for (const r of rs) { num += (r.t - mx) * (toMiB(r.footprintKb) - my); den += (r.t - mx) ** 2; }
    return den === 0 ? 0 : (num / den) * 60;
  };
  const q = (rs, f) => { const a = rs.map(f).sort((x, y) => x - y); return { count: a.length, mean: a.reduce((p, c) => p + c, 0) / a.length, p50: quantile(a, 0.5), p95: quantile(a, 0.95), p99: quantile(a, 0.99), max: a[a.length - 1] }; };

  // —— measure 全段（SL-C04/05/06/09/10 口径）——
  const first = rows[0], last = rows[rows.length - 1];
  const durSec = last.t - first.t;
  const measure = {
    samples: rows.length,
    durSec,
    displayFps: (last.displayed - first.displayed) / durSec,
    producerFps: (last.published - first.published) / durSec,
    producerRenderedFps: (last.producerRendered - first.producerRendered) / durSec,
    producerFailed: last.producerFailed - first.producerFailed,
    dropped: last.dropped - first.dropped,
    upload: (() => {
      const c = last.uploadCount - first.uploadCount, s = last.uploadSumUs - first.uploadSumUs;
      return { count: c, meanMs: c > 0 ? s / c / 1000 : NaN, maxMs: Math.max(...rows.map((r) => r.uploadMaxMs)) };
    })(),
    age: q(rows, (r) => r.ageMs),
    mem: { firstMiB: toMiB(first.footprintKb), lastMiB: toMiB(last.footprintKb), slopeMiBPerMin: slopeOf(rows), rssFirstMiB: toMiB(first.rssKb), rssLastMiB: toMiB(last.rssKb) },
    exposedRatio: rows.filter((r) => r.exposed).length / rows.length,
    degradedRatio: rows.filter((r) => r.degraded).length / rows.length,
  };

  // —— 稳态窗（后 2/3；SL-C01/02/03/07/11 口径）——
  const st = rows.filter((r) => r.steady);
  const steady = st.length < 2 ? null : {
    samples: st.length,
    startSec: st[0].t,
    durSec: st[st.length - 1].t - st[0].t,
    displayFps: (st[st.length - 1].displayed - st[0].displayed) / (st[st.length - 1].t - st[0].t),
    producerFps: (st[st.length - 1].published - st[0].published) / (st[st.length - 1].t - st[0].t),
    age: q(st, (r) => r.ageMs),
    mem: { firstMiB: toMiB(st[0].footprintKb), lastMiB: toMiB(st[st.length - 1].footprintKb), slopeMiBPerMin: slopeOf(st) },
  };

  return { measure, steady };
}

// ── 解析逐帧 CSV（两种 schema，按列名自动判别）──────────────────────────────
//   · 消费侧（SkyLongRun，P-BRG-01）：t_s,phase,frame_number,interval_ms      —— 上屏间隔
//   · 产帧侧（LegacyLongRun，T9 原 GL 基线 · 桥接成本）：t_s,phase,frame_number,
//       render_ms,readback_ms,total_ms,mailbox_complete,mailbox_age_ms,rss_kb,footprint_kb
//     —— render=引擎 GL 出图、readback=GPU→CPU 读回、total=两者之和
//   T9 侧**无稳态窗**（其判据按 measure 全段口径出数），故只报 measure 全段。
function parsePerFrame(text, steadyStartSec) {
  const lines = text.split('\n').filter((l) => l.trim().length > 0);
  const cols = lines[0].split(',').map((c) => c.trim());
  const idx = Object.fromEntries(cols.map((c, i) => [c, i]));
  const consumer = idx.interval_ms !== undefined;
  const producer = idx.render_ms !== undefined;

  const stat = (a) => {
    if (a.length === 0) return null;
    a = a.slice().sort((x, y) => x - y);
    return { count: a.length, mean: a.reduce((p, c) => p + c, 0) / a.length, p50: quantile(a, 0.5), p95: quantile(a, 0.95), p99: quantile(a, 0.99), max: a[a.length - 1] };
  };
  const collect = (key, minExclusive = null) => {
    const all = [], st = [];
    for (let i = 1; i < lines.length; i++) {
      const f = lines[i].split(',');
      if (f.length < cols.length) continue;
      if (f[idx.phase].trim() !== 'measure') continue;
      const v = +f[idx[key]];
      if (!Number.isFinite(v)) continue;
      if (minExclusive !== null && !(v > minExclusive)) continue;
      all.push(v);
      if (+f[idx.t_s] >= steadyStartSec) st.push(v);
    }
    return { measure: stat(all), steady: stat(st) };
  };

  if (consumer) return { kind: 'consumer', interval: collect('interval_ms', 0) };
  if (producer) {
    return {
      kind: 'producer',
      render: collect('render_ms'),
      readback: collect('readback_ms'),
      total: collect('total_ms'),
      age: collect('mailbox_age_ms'),
    };
  }
  return null;
}

// ── 组装 ────────────────────────────────────────────────────────────────────
const logText = readMaybeGz(opts.log);
const log = parseLog(logText);

// 稳态窗起点 = warmup + measure/3。measure 秒数由 CSV 末点推得（measure 段结束于
// warmup+measure）—— 先轻扫一遍拿末端时间戳，避免"解析前就要窗口"的鸡生蛋。
function steadyStartFromCsv(text, warmupSec) {
  let lastMeasureT = NaN;
  const lines = text.split('\n');
  const cols = lines[0] ? lines[0].split(',').map((c) => c.trim()) : [];
  const iP = cols.indexOf('phase'), iT = cols.indexOf('t_s');
  if (iP < 0 || iT < 0) return NaN;
  for (const l of lines) {
    const f = l.split(',');
    if (f.length <= iP || f[iP].trim() !== 'measure') continue;
    const t = +f[iT];
    if (Number.isFinite(t)) lastMeasureT = t;
  }
  if (!Number.isFinite(lastMeasureT)) return NaN;
  const measureSec = lastMeasureT - warmupSec;
  return warmupSec + measureSec / 3;
}

const csvText = opts.csv ? readMaybeGz(opts.csv) : null;
const framesText = opts.frames ? readMaybeGz(opts.frames) : null;
const steadyStart = csvText && log.header ? steadyStartFromCsv(csvText, log.header.warmupSec) : NaN;
const perSec = csvText ? parsePerSecond(csvText, steadyStart) : null;
const perFrame = framesText ? parsePerFrame(framesText, steadyStart) : null;

const mismatches = [];
if (perSec && log.judge.length) {
  const s = perSec.steady;
  const c02 = pick(log.judge, 'SL-C02');
  if (c02 && s) {
    const m = c02.detail.match(/([\d.]+)\s*fps/);
    if (m && Math.abs(+m[1] - s.displayFps) > 0.15)
      mismatches.push(`SL-C02 日志 ${m[1]} fps vs CSV 稳态窗复算 ${fmt(s.displayFps)} fps`);
  }
  const c07 = pick(log.judge, 'SL-C07');
  if (c07 && s) {
    const m = c07.detail.match(/斜率\s+(-?[\d.]+)\s*MiB\/min/);
    if (m && Math.abs(+m[1] - s.mem.slopeMiBPerMin) > 0.02)
      mismatches.push(`SL-C07 日志 ${m[1]} vs CSV 稳态窗复算 ${fmt(s.mem.slopeMiBPerMin, 3)} MiB/min`);
  }
}

const out = [];
const L = (s = '') => out.push(s);
const label = opts.label || path.basename(opts.log);
L(`### ${label}`);
L();
L(`来源：\`${path.relative(process.cwd(), opts.log)}\``);
if (log.header) L(`跑法：预热 ${log.header.warmupSec}s + 测量 ${perSec ? fmt(perSec.measure.durSec, 0) : '?'}s，${log.header.size}，生产者名义 ${log.header.nominalFps} fps`);
if (perSec && perSec.steady)
  L(`稳态窗：t ≥ ${fmt(perSec.steady.startSec, 0)}s（= 预热 + 测量/3，后 2/3 段，${perSec.steady.samples} 点）`);
L();

if (!perSec && !perFrame && log.judge.length === 0) {
  L('> ⚠ 未解析到任何 STELLRUN 判据行，也无 CSV —— 检查日志是否被截断。');
}

// 判据结论表
if (log.judge.length) {
  L('| 判据 | 结论 | 详情 |');
  L('|---|---|---|');
  for (const j of log.judge) L(`| ${j.id} | **${j.pass}** | ${j.detail.replace(/\|/g, '\\|')} |`);
  L();
  if (log.verdict) L(`\`VERDICT=${log.verdict}\``);
  L();
}

// 复算读数表
if (perSec) {
  const m = perSec.measure, s = perSec.steady;
  L('**CSV 复算读数**（独立于日志结论，用于核对）：');
  L();
  L('| 量 | 稳态窗（后 2/3） | measure 全段 |');
  L('|---|---|---|');
  L(`| 采样点 / 窗口 | ${s ? `${s.samples} 点 / ${fmt(s.durSec, 0)} s` : '—'} | ${m.samples} 点 / ${fmt(m.durSec, 0)} s |`);
  L(`| 显示帧率 | ${s ? `**${fmt(s.displayFps)} fps**` : '—'} | ${fmt(m.displayFps)} fps |`);
  L(`| 生产者产帧率 | ${s ? `${fmt(s.producerFps)} fps` : '—'} | ${fmt(m.producerFps)} fps（渲染 ${fmt(m.producerRenderedFps)}，失败 ${m.producerFailed}） |`);
  L(`| 邮箱丢弃 | — | **${m.dropped}** |`);
  L(`| 上传（读回→上传） | — | ${m.upload.count} 次；均值 **${fmt(m.upload.meanMs, 3)} ms**、最坏 ${fmt(m.upload.maxMs, 2)} ms |`);
  L(`| 邮箱帧龄（逐秒口径） | ${s ? `mean ${fmt(s.age.mean)} / p95 ${fmt(s.age.p95)} / max ${fmt(s.age.max)} ms` : '—'} | mean ${fmt(m.age.mean)} / p50 ${fmt(m.age.p50)} / p95 ${fmt(m.age.p95)} / p99 ${fmt(m.age.p99)} / max ${fmt(m.age.max)} ms |`);
  L(`| 内存 phys_footprint | ${s ? `${fmt(s.mem.firstMiB, 1)} → ${fmt(s.mem.lastMiB, 1)} MiB；斜率 **${fmt(s.mem.slopeMiBPerMin, 3)} MiB/min**` : '—'} | ${fmt(m.mem.firstMiB, 1)} → ${fmt(m.mem.lastMiB, 1)} MiB；斜率 ${fmt(m.mem.slopeMiBPerMin, 3)} MiB/min |`);
  L(`| RSS（参考，不判泄漏） | — | ${fmt(m.mem.rssFirstMiB, 1)} → ${fmt(m.mem.rssLastMiB, 1)} MiB |`);
  L(`| 窗口暴露 | — | ${fmt(m.exposedRatio * 100, 1)}%（被遮挡/最小化则数据不可信） |`);
  L(`| 降级误报 | — | ${fmt(m.degradedRatio * 100, 3)}% |`);
  L();
}
if (perFrame) {
  if (perFrame.kind === 'consumer') {
    const f = perFrame.interval.steady || perFrame.interval.measure;
    L(`**上屏间隔分位**（逐帧 CSV）：稳态窗 ${f.count} 帧 mean ${fmt(f.mean)} / p50 ${fmt(f.p50)} / p95 ${fmt(f.p95)} / p99 ${fmt(f.p99)} / max ${fmt(f.max)} ms`);
    if (perFrame.interval.measure && perFrame.interval.steady)
      L(`（measure 全段 ${perFrame.interval.measure.count} 帧：mean ${fmt(perFrame.interval.measure.mean)} / p95 ${fmt(perFrame.interval.measure.p95)} / p99 ${fmt(perFrame.interval.measure.p99)} / max ${fmt(perFrame.interval.measure.max)} ms）`);
    L();
  } else if (perFrame.kind === 'producer') {
    L('**产帧侧逐帧耗时**（measure 全段；T9 侧无稳态窗口径）：');
    L();
    L('| 量 | mean | p50 | p95 | max |');
    L('|---|---|---|---|---|');
    for (const [name, s] of [['render（引擎 GL 出图）', perFrame.render], ['readback（GPU→CPU 读回）', perFrame.readback], ['total（render+readback）', perFrame.total], ['mailbox_age（邮箱帧龄）', perFrame.age]]) {
      if (s && s.measure) L(`| ${name} | ${fmt(s.measure.mean)} | ${fmt(s.measure.p50)} | ${fmt(s.measure.p95)} | ${fmt(s.measure.max)} ms |`);
    }
    L();
    L('> **桥接独立成本** = `readback`（读回）+ 消费侧 `上传`（见上表）。这两项是帧桥**新增**的开销，');
    L('> 原版 Stellarium 直接上屏没有它们。');
    L();
  }
}

// SL-C11 的高频帧龄分布不在 CSV 里，单独摘出来
const c11 = pick(log.judge, 'SL-C11');
if (c11) {
  L(`> **SL-C11 高频帧龄**（100ms 粒度采样，不入 CSV，只此一处）：${c11.detail}`);
  L();
}

if (mismatches.length) {
  L(`> ⚠ **日志与 CSV 复算不一致**（须人工定性，不得静默）：`);
  for (const m of mismatches) L(`> - ${m}`);
  L();
}

if (log.info.length) {
  L('留证文件（长跑自报）：');
  for (const i of log.info) L(`- ${i}`);
  L();
}
if (log.notes.length) {
  L('其他自报行：');
  for (const n of log.notes) L(`- ${n}`);
  L();
}

const text = out.join('\n');
process.stdout.write(text + '\n');

if (opts.json) {
  fs.writeFileSync(opts.json, JSON.stringify({ label, log, perSec, perFrame, mismatches }, null, 2));
  console.error(`[json] 写入 ${opts.json}`);
}
if (mismatches.length) process.exitCode = 1;
