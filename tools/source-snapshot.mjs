// Audit a local extraction. Does not modify the original tree or resource links.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = '/Users/ztuqfvy/qt_demo/stellarium/stellarium';
const manifestPath = path.join(root, 'docs/vulkan/source-manifest.json');
const dirs = ['src','plugins','cmake','data','android','util','scripts','doc','guide'];
const files = ['CMakeLists.txt','BUILDING.md','CONTRIBUTING.md','COPYING','CREDITS.md','ChangeLog','README.md','BACKERS.md','CHECKLIST.yml','CITATION','CITATION.cff','MAINTAINER_BUSINESS.md','codemeta.json','transifex.yml','.clang-format','.clang-tidy','.appveyor.yml','.whitesource'];
const resources = ['models','textures','landscapes','skycultures','nebulae','stars','atmosphere','scenery3d','po'];
function list(base, rel) {
  const full = path.join(base, rel), stat = fs.lstatSync(full);
  if (stat.isSymbolicLink()) throw new Error(`Unexpected nested symlink: ${full}`);
  if (stat.isDirectory()) return fs.readdirSync(full).filter(n => n !== '.DS_Store').sort().flatMap(n => list(base, path.join(rel,n)));
  if (!stat.isFile()) throw new Error(`Not a regular file: ${full}`);
  return [rel];
}
function digest(base, rel) {
  const data = fs.readFileSync(path.join(base,rel));
  return { path: rel, bytes: data.length, sha256: crypto.createHash('sha256').update(data).digest('hex') };
}
const mode = process.argv[2] ?? 'verify';
if (mode === 'create') {
  if (fs.existsSync(manifestPath)) throw new Error('Manifest exists; refusing to overwrite baseline.');
  const entries = [...dirs.flatMap(d => list(source,d)), ...files].sort().map(p => digest(source,p));
  for (const e of entries) if (digest(root,e.path).sha256 !== e.sha256) throw new Error(`Copy differs: ${e.path}`);
  const links = resources.map(p => {
    const target = path.join(source,p);
    if (fs.readlinkSync(path.join(root,p)) !== target) throw new Error(`Unexpected link: ${p}`);
    return { path:p, target, files:list(source,p).map(f => digest(source,f)) };
  });
  const out = { generatedAt:new Date().toISOString(), source, destination:root, provenance:'Local CMake 26.1 source snapshot; no Git commit available', copiedFiles:entries, resources:links };
  fs.mkdirSync(path.dirname(manifestPath),{recursive:true});
  fs.writeFileSync(manifestPath, JSON.stringify(out,null,2)+'\n',{flag:'wx'});
  console.log(JSON.stringify({copiedFiles:entries.length,copiedBytes:entries.reduce((s,e)=>s+e.bytes,0),resourceFiles:links.reduce((s,e)=>s+e.files.length,0),resourceBytes:links.reduce((s,e)=>s+e.files.reduce((t,f)=>t+f.bytes,0),0),manifest:manifestPath},null,2));
} else if (mode === 'verify') {
  const m = JSON.parse(fs.readFileSync(manifestPath,'utf8'));
  let count=0;
  for (const e of [...m.copiedFiles,...m.resources.flatMap(r=>r.files)]) {
    if (digest(root,e.path).sha256 !== e.sha256) throw new Error(`Extracted file changed: ${e.path}`);
    if (fs.existsSync(m.source) && digest(m.source,e.path).sha256 !== e.sha256) throw new Error(`Original changed after extraction: ${e.path}`);
    count++;
  }
  for (const r of m.resources) {
    const stat=fs.lstatSync(path.join(root,r.path));
    if (stat.isSymbolicLink() && fs.readlinkSync(path.join(root,r.path)) !== r.target) throw new Error(`Resource link changed: ${r.path}`);
  }
  console.log(`PASS: ${count} source/resource files match SHA-256 baseline. This is an extraction check, not a build test.`);
} else throw new Error('Usage: node tools/source-snapshot.mjs create|verify');
