// Convert only the nine documented resource links to independent directories.
// Copies and verifies into a temporary sibling before replacing each symlink.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if (process.argv[2] !== '--copy') throw new Error('Usage: node tools/materialize-resources.mjs --copy (needs about 1 GiB free space)');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'docs/vulkan/source-manifest.json'),'utf8'));
for (const r of manifest.resources) {
  const dest=path.join(root,r.path), stat=fs.lstatSync(dest);
  if (!stat.isSymbolicLink()) { console.log(`Already independent: ${r.path}`); continue; }
  if (fs.readlinkSync(dest)!==r.target) throw new Error(`Unexpected link: ${dest}`);
  const temp=fs.mkdtempSync(path.join(root,`.materialize-${r.path}-`));
  const copy=path.join(temp,'content');
  fs.cpSync(r.target,copy,{recursive:true,dereference:false,filter:p=>path.basename(p)!=='.DS_Store'});
  for (const f of r.files) {
    const local=path.relative(r.path,f.path);
    const hash=crypto.createHash('sha256').update(fs.readFileSync(path.join(copy,local))).digest('hex');
    if(hash!==f.sha256) throw new Error(`Resource changed: ${f.path}. Original link retained; inspect ${temp}.`);
  }
  // Only unlink the known symlink, never its target. Restore it if rename fails.
  fs.unlinkSync(dest);
  try { fs.renameSync(copy,dest); } catch(e) { fs.symlinkSync(r.target,dest); throw e; }
  fs.rmdirSync(temp);
  console.log(`Copied and verified: ${r.path}`);
}
