// Hypothetical sensitivity analysis, NOT measured Stellarium performance.
const cases = [
  ['GPU limited',4,2,10,2,.3],
  ['CPU submission limited',4,8,6,2,.5],
  ['Astronomy CPU limited',12,2,6,2,.5],
  ['Heavy GPU scene',3,3,20,2,.5],
  ['Extra integration overhead',3,2,5,2,1.5],
];
console.table(cases.map(([scenario,logic,submit,gpu,k,overhead])=>{
  const before=Math.max(logic+submit,gpu);
  const after=Math.max(logic+submit/k+overhead,gpu);
  return {scenario,logic_ms:logic,submit_ms:submit,gpu_ms:gpu,new_overhead_ms:overhead,before_ms:before,after_ms:after,old_fps:+(1000/before).toFixed(1),new_fps:+(1000/after).toFixed(1),fps_gain_pct:+((before/after-1)*100).toFixed(1)};
}));
console.log('Assumptions: submission work halved; unchanged GPU cost; CPU/GPU overlap; no vsync or frame cap. Not benchmark results.');
