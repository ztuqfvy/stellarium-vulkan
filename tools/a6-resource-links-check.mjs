// A6 资源链接完整性检查（测试文档 §6.3 P-CFG-03「资源目录保护」的判据）。
//
// 出处：`P-CFG-03 | 资源目录保护 | 只读资源链接不被生成工具改写；materialize 前对
//       链接目录的写操作被流程禁止`。
//
// 分工（为什么是两段）：
//   · 本脚本 = **机械事实**：资源路径此刻是不是符号链接、指向哪、target 在不在、
//     有没有 materialize 临时残骸、git 有没有在资源路径下报改动。
//   · `tools/source-snapshot.mjs verify` = **内容级**校验（逐文件 sha256、4150 个文件）。
//     两者不重复：前者快（秒级）且能在每次批跑里挂上，后者重（全量哈希）。
//
// ⚠️ 两套清单不可混：`setup-upstream-assets.sh` 实际建 **12 个**链接目录
//    （atmosphere landscapes models nebulae po scenery3d skycultures stars textures
//      plugins scripts util）+ `data/` **实体副本**（CMake 配置期会写它，软链会穿到上游）；
//    而 `source-manifest.json` 只覆盖其中 **9 个**（有逐文件 sha256 的那批）。
//    故本脚本 §1 按**清单**逐项校验，§1b 把**差额**与 `data/` 的性质也如实报出。
//
// 用法：
//   node tools/a6-resource-links-check.mjs            # 结构检查（默认）
//   node tools/a6-resource-links-check.mjs --deep     # 追加全量 sha256 校验（慢）
//   node tools/a6-resource-links-check.mjs --json out.json
//
// 退出码：0 = PASS；1 = FAIL（有违规）；2 = 用法错。

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const argv = process.argv.slice(2);
const deep = argv.includes('--deep');
const jsonIdx = argv.indexOf('--json');
const jsonOut = jsonIdx >= 0 ? argv[jsonIdx + 1] : null;

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const manifestPath = path.join(root, 'docs/vulkan/source-manifest.json');
if (!fs.existsSync(manifestPath)) {
  console.error(`FAIL 找不到清单：${manifestPath}`);
  process.exit(1);
}
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));

const fails = [];
const notes = [];
const P = (s) => console.log(s);

P('════ A6 资源链接完整性检查（P-CFG-03）════');
P(`清单：${path.relative(root, manifestPath)}｜生成于 ${manifest.generatedAt ?? '(未记)'}`);
P(`源快照：${manifest.source ?? '(未记)'}`);
P('');

// ── 1. 九个资源路径：必须是符号链接、target 与清单一致、target 存在 ──────────
P(`── 1. 资源占位（${manifest.resources.length} 项）`);
let nLinks = 0, nDirs = 0, nMissing = 0;
for (const r of manifest.resources) {
  const p = path.join(root, r.path);
  let st;
  try {
    st = fs.lstatSync(p);
  } catch {
    ++nMissing;
    fails.push(`资源 ${r.path}：路径不存在（清单要求符号链接 → ${r.target}）`);
    P(`  ✗ ${r.path.padEnd(14)} 不存在`);
    continue;
  }
  if (st.isSymbolicLink()) {
    const tgt = fs.readlinkSync(p);
    // manifest 记的是绝对路径；仍用 resolve(root, tgt) 兜住相对形式的链接
    const tgtAbs = path.resolve(root, tgt);
    const tgtOk = tgt === r.target;
    const tgtExists = fs.existsSync(tgtAbs) && fs.statSync(tgtAbs).isDirectory();
    if (!tgtOk) fails.push(`资源 ${r.path}：链接目标不符（实际 ${tgt} ≠ 清单 ${r.target}）`);
    if (!tgtExists) fails.push(`资源 ${r.path}：链接目标不存在（${tgt}）`);
    if (tgtOk && tgtExists) ++nLinks;
    const mt = tgtExists
      ? new Date(fs.statSync(tgtAbs).mtimeMs).toISOString().replace('T', ' ').slice(0, 19)
      : '—';
    P(`  ${tgtOk && tgtExists ? '✓' : '✗'} ${r.path.padEnd(14)} 链接 → ${tgt}｜target mtime ${mt}｜清单文件数 ${r.files.length}`);
  } else {
    ++nDirs;
    fails.push(`资源 ${r.path}：**已不是符号链接**（是实体${st.isDirectory() ? '目录' : '文件'}）—— 疑似被 materialize 或生成工具改写`);
    P(`  ✗ ${r.path.padEnd(14)} 实体${st.isDirectory() ? '目录' : '文件'}（应为符号链接）`);
  }
}
P(`  小结：符号链接 ${nLinks} / 实体 ${nDirs} / 缺失 ${nMissing}（期望 9/0/0）`);
P('');

// ── 1b. 仓库根下的全部符号链接（清单之外的也要看得见）──────────────────────
// 为什么要有这一段：`setup-upstream-assets.sh` 实际建的是 **12 个**链接目录
// （atmosphere landscapes models nebulae po scenery3d skycultures stars textures
//  plugins scripts util）+ `data/` **实体副本**（配置期会被 CMake 写入，软链会穿到上游）。
// 而 `source-manifest.json` 只覆盖其中 **9 个**（有逐文件 sha256 的那批）。
// ⇒ 差额必须如实报出来，否则"9/9 全好"会掩盖另外 3 个链接的状态。
P('── 1b. 仓库根下全部符号链接（清单外的也列出）');
const manifestPaths = new Set(manifest.resources.map((r) => r.path));
const rootLinks = fs
  .readdirSync(root)
  .filter((n) => {
    try { return fs.lstatSync(path.join(root, n)).isSymbolicLink(); } catch { return false; }
  })
  .sort();
const extraLinks = rootLinks.filter((n) => !manifestPaths.has(n));
P(`  符号链接共 ${rootLinks.length} 个（清单内 ${rootLinks.length - extraLinks.length}，清单外 ${extraLinks.length}）`);
for (const n of extraLinks) {
  const tgt = fs.readlinkSync(path.join(root, n));
  const ok = fs.existsSync(path.resolve(root, tgt));
  P(`  ${ok ? '·' : '✗'} ${n.padEnd(14)} → ${tgt}${ok ? '' : '  (**target 不存在**)'}`);
  if (!ok) fails.push(`清单外链接 ${n} 的 target 不存在：${tgt}`);
}
for (const n of manifestPaths) {
  if (!rootLinks.includes(n)) P(`  ⚠ 清单内 ${n} 不在根下符号链接列表中（已被 §1 判为实体/缺失）`);
}
// `data` 应是**实体副本**（不是链接）—— 软链会让 CMake 配置期写穿上游
{
  const dp = path.join(root, 'data');
  if (fs.existsSync(dp)) {
    const isLink = fs.lstatSync(dp).isSymbolicLink();
    P(`  ${isLink ? '✗' : '✓'} data/ = ${isLink ? '**符号链接（危险！CMake 配置期会写穿上游）**' : '实体副本（正确）'}`);
    if (isLink) fails.push('data/ 是符号链接：根 CMake 配置期会写入 data/default_cfg.ini 等，软链会穿透改写上游源码树');
  } else {
    P('  · data/ 不存在（根构建未配置过；src/ui 独立工程不需要它）');
  }
}
P('');

// ── 2. materialize 临时残骸 ─────────────────────────────────────────────────
P('── 2. materialize 临时残骸');
const stray = fs.readdirSync(root).filter((n) => n.startsWith('.materialize-'));
if (stray.length) {
  fails.push(`发现 materialize 临时残骸 ${stray.length} 个：${stray.slice(0, 5).join(', ')}`);
  P(`  ✗ ${stray.join(', ')}`);
} else {
  P('  ✓ 无 .materialize-* 残骸');
}
P('');

// ── 3. git 在资源路径下是否报改动 ──────────────────────────────────────────
P('── 3. git 是否在资源路径下报改动（链接被写会在这里露头）');
let gitPorcelain = '';
let gitOk = true;
try {
  gitPorcelain = execFileSync('git', ['status', '--porcelain'], { cwd: root, encoding: 'utf8' });
} catch (e) {
  gitOk = false;
  notes.push('git status 不可用（非 git 仓库或 git 缺失）—— 本项跳过');
}
const resPaths = manifest.resources.map((r) => r.path + '/');
const dirtyRes = gitPorcelain
  .split('\n')
  .filter((l) => l.trim())
  .filter((l) => resPaths.some((rp) => l.slice(3).trim().startsWith(rp)));
// ⚠️ 不能用 `!gitPorcelain` 判"git 不可用"：工作树干净时输出就是空串（falsy），
//    会把"0 处改动"误报成"跳过"（首版就踩了这个）。
if (!gitOk) {
  P('  ⚠ 跳过（git 不可用）');
} else if (dirtyRes.length) {
  fails.push(`资源路径下有 ${dirtyRes.length} 处 git 改动（链接目录不该被写）：\n    ${dirtyRes.slice(0, 8).join('\n    ')}`);
  P(`  ✗ ${dirtyRes.length} 处改动`);
  for (const l of dirtyRes.slice(0, 8)) P(`      ${l}`);
} else {
  P('  ✓ 资源路径下 0 处改动');
}
P('');

// ── 4.（可选）全量 sha256 ──────────────────────────────────────────────────
if (deep) {
  P('── 4. 全量 sha256 校验（--deep）');
  let checked = 0, bad = 0;
  const t0 = Date.now();
  for (const r of manifest.resources) {
    for (const f of r.files) {
      const abs = path.join(root, f.path);
      try {
        const h = crypto.createHash('sha256').update(fs.readFileSync(abs)).digest('hex');
        if (h !== f.sha256) {
          ++bad;
          if (bad <= 5) fails.push(`文件内容变化：${f.path}（清单 ${f.sha256.slice(0, 12)}… ≠ 实际 ${h.slice(0, 12)}…）`);
        }
        ++checked;
      } catch (e) {
        ++bad;
        if (bad <= 5) fails.push(`文件不可读：${f.path}（${e.code ?? e.message}）`);
      }
    }
  }
  P(`  校验 ${checked} 个文件，不符 ${bad} 个（耗时 ${((Date.now() - t0) / 1000).toFixed(1)}s）`);
  P('');
  if (bad) P(`  ✗ 有 ${bad} 个文件与清单不符 —— 资源被改写`);
  else P('  ✓ 全部与清单逐字节一致');
} else {
  P('── 4. 全量 sha256（未启用；加 --deep 才跑）');
  P('  提示：内容级校验也可直接跑 `node tools/source-snapshot.mjs verify`');
  P('');
}

// ── 结论 ───────────────────────────────────────────────────────────────────
P('════ 结论 ════');
for (const n of notes) P(`  ℹ ${n}`);
if (fails.length === 0) {
  P('  VERDICT=PASS（资源链接结构完好；materialize 临时残骸 0；资源路径无 git 改动）');
} else {
  P(`  VERDICT=FAIL（${fails.length} 项违规）`);
  for (const f of fails) P(`  ✗ ${f}`);
}

if (jsonOut) {
  fs.writeFileSync(jsonOut, JSON.stringify({ deep, fails, notes, nLinks, nDirs, nMissing }, null, 2));
  console.error(`[json] 写入 ${jsonOut}`);
}
process.exit(fails.length ? 1 : 0);
