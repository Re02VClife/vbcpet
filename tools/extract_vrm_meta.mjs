#!/usr/bin/env node
// extract_vrm_meta.mjs — 解析 .vrm (GLB 容器) 的 VRM 扩展，打印元数据 + blendShapeMaster 表情组
// 用法: node tools/extract_vrm_meta.mjs <path.vrm>
import { readFileSync } from 'node:fs';

const file = process.argv[2];
if (!file) { console.error('usage: node extract_vrm_meta.mjs <path.vrm>'); process.exit(1); }
const buf = readFileSync(file);
if (buf.readUInt32LE(0) !== 0x46546C67) { console.error('not a GLB container'); process.exit(1); }

let off = 12;
let json = null;
while (off < buf.length) {
  const len = buf.readUInt32LE(off);
  const type = buf.toString('utf8', off + 4, off + 8);
  const data = buf.subarray(off + 8, off + 8 + len);
  if (type === 'JSON') json = JSON.parse(data.toString('utf8'));
  off += 8 + len;
}
if (!json) { console.error('no JSON chunk'); process.exit(1); }

const vrm = json.extensions && json.extensions.VRM;
console.log('== meta ==');
console.log(JSON.stringify(vrm ? (vrm.meta || {}) : {}, null, 1));
if (!vrm) { console.log('(no extensions.VRM)'); process.exit(0); }

const groups = (vrm.blendShapeMaster && vrm.blendShapeMaster.blendShapeGroups) || [];
console.log(`\n== blendShapeGroups: ${groups.length} ==`);
for (const g of groups) {
  const n = (g.binds || []).length;
  console.log(`  ${String(g.name).padEnd(20)} presetName=${String(g.presetName).padEnd(12)} binds=${n}`);
}
