// SPDX-License-Identifier: GPL-3.0-or-later
(() => {
  const el=id=>document.getElementById(id);
  let snapshot=null,endpoint='',sending=false;
  const errors=[];
  window.addEventListener('error',e=>{errors.push(String(e.message).slice(0,2000));if(errors.length>20)errors.shift();});
  window.addEventListener('unhandledrejection',e=>{errors.push(String(e.reason?.message||e.reason).slice(0,2000));if(errors.length>20)errors.shift();});
  function enabled(){el('sendReport').disabled=sending||!snapshot||!endpoint||!el('reportConsent').checked||el('reportTitle').value.trim().length<5||el('reportDescription').value.trim().length<10;}
  async function image(screen){
    const canvas=await html2canvas(document.querySelector('main.shell'),{scale:1,logging:false,backgroundColor:'#101827',onclone:doc=>{
      doc.querySelectorAll('dialog').forEach(d=>d.remove());
      doc.getElementById('setupScreen').hidden=screen!=='setup';
      doc.getElementById('progressScreen').hidden=screen!=='progress';
      ['setup','progress'].forEach(s=>{const tab=doc.getElementById(s+'Tab');tab.classList.toggle('selected',s===screen);tab.setAttribute('aria-selected',String(s===screen));});
    }});
    return {tab:screen,mime:'image/png',dataUrl:canvas.toDataURL('image/png')};
  }
  async function collect(){
    snapshot=null;enabled();el('reportStatus').textContent='Collecting diagnostics and app screenshots…';
    const dropdowns=[...document.querySelectorAll('select')].map(s=>({id:s.id,label:document.querySelector('label[for="'+s.id+'"]')?.textContent||s.id,value:s.value,disabled:s.disabled,options:[...s.options].map(o=>({label:o.text,value:o.value,disabled:o.disabled,hidden:o.hidden}))}));
    const fields=[...document.querySelectorAll('main input, #pathsDialog input')].map(i=>({id:i.id,value:i.type==='checkbox'?i.checked:i.value,disabled:i.disabled}));
    const diagnostics={schema:1,appVersion:document.querySelector('.version')?.textContent||'unknown',capturedAt:new Date().toISOString(),games:JSON.parse(JSON.stringify(games)),storageDevices:JSON.parse(JSON.stringify(tvAllDevices)),dropdowns,fields,browser:{userAgent:navigator.userAgent,errors:[...errors]},visibleStatus:{}};
    for(const id of ['phase','mountState','startRequirements','jobDescription','jobNumbers','jobTime','jobPerformance','jobFile','jobDestination'])diagnostics.visibleStatus[id]=el(id)?.textContent||'';
    try{diagnostics.console=await api('/api/gc/ampr-lz4/report', 'GET',10000);diagnostics.firmware=diagnostics.console.firmware;}catch(e){diagnostics.console={unavailable:String(e.message)};}
    diagnostics.screenshots=[];
    for(const screen of ['setup','progress'])try{diagnostics.screenshots.push(await image(screen));}catch(e){diagnostics.screenshots.push({tab:screen,error:String(e.message)});}
    snapshot=diagnostics;
    const previews=el('reportImages');previews.replaceChildren();
    for(const shot of diagnostics.screenshots)if(shot.dataUrl){const img=new Image();img.src=shot.dataUrl;img.alt=shot.tab+' tab screenshot';img.style.cssText='width:47%;max-height:130px;object-fit:contain';previews.append(img);}
    el('reportStatus').textContent=`Includes ${diagnostics.games.length} games, ${diagnostics.storageDevices.length} storage devices, settings, logs, and ${diagnostics.screenshots.filter(s=>s.dataUrl).length}/2 app screenshots. ${endpoint?'Ready for review.':'Sending is not activated yet; you can save the report.'}`;
    el('saveReport').disabled=false;enabled();
  }
  function report(){return {schema:1,consent:el('reportConsent').checked,title:el('reportTitle').value.trim(),description:el('reportDescription').value.trim(),contact:el('reportContact').value.trim(),diagnostics:snapshot};}
  el('reportIssue').onclick=()=>{tvOpen('reportDialog');el('reportConsent').checked=false;collect().catch(e=>{el('reportStatus').textContent='Could not collect report: '+e.message;});};
  for(const id of ['reportTitle','reportDescription','reportContact','reportConsent'])el(id).addEventListener('input',enabled);
  el('saveReport').onclick=()=>{if(!snapshot)return;const url=URL.createObjectURL(new Blob([JSON.stringify(report(),null,2)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download='lz4-issue-report.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
  el('sendReport').onclick=async()=>{
    if(el('sendReport').disabled)return;sending=true;enabled();el('reportStatus').textContent='Sending private report…';
    try{const body=JSON.stringify(report());if(new TextEncoder().encode(body).length>13*1024*1024)throw new Error('Report exceeds upload size. Save it instead.');const response=await fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json'},body,signal:AbortSignal.timeout(90000)});const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'Submission failed');el('reportStatus').textContent='Private report sent. Report ID: '+result.reportId;snapshot=null;}catch(e){el('reportStatus').textContent=e.message+' You can save the report and retry later.';}finally{sending=false;enabled();}
  };
  fetch('/report-config.json').then(r=>r.json()).then(c=>{if(c.enabled&&/^https:\/\/[-a-z0-9]+\.supabase\.co\/functions\/v1\/report-issue$/.test(c.endpoint))endpoint=c.endpoint;enabled();}).catch(()=>{});
})();
