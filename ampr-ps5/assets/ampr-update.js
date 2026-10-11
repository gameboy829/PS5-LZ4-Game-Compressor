// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
let updateTimer=0,updateRestarting=false,updateRestartBegan=0,updateCurrent='';
function renderUpdate(s){
 updateCurrent=s.currentVersion;byId('updateVersion').textContent=`Installed version: ${s.currentVersion}`;
 const bytes=s.downloadedBytes||0,total=s.release?.size||0;
 byId('updateStatus').textContent=s.message||s.lastResult?.message||'Check for a new release.';
 byId('checkUpdate').disabled=Boolean(s.busy||updateRestarting);
 byId('installUpdate').disabled=Boolean(!s.available||s.busy||updateRestarting);
 byId('updateProgress').hidden=!s.busy;byId('updateProgress').value=total?Math.min(100,bytes/total*100):0;
 byId('updates').textContent=s.available?'Update available':'Updates';
 if(s.phase==='restarting'&&!updateRestarting){updateRestarting=true;updateRestartBegan=Date.now();}
}
async function pollUpdate(){
 clearTimeout(updateTimer);
 try{const s=await api('/api/gc/ampr-lz4/update');
  if(updateRestarting&&s.currentVersion!==updateCurrent){location.reload();return;}
  if(updateRestarting&&s.lastResult?.phase==='error'){updateRestarting=false;s.message=s.lastResult.message;}
  renderUpdate(s);if(s.busy||updateRestarting)updateTimer=setTimeout(pollUpdate,1500);
 }catch(e){
  if(updateRestarting&&Date.now()-updateRestartBegan<90000){byId('updateStatus').textContent='Restarting the app. Reconnecting...';updateTimer=setTimeout(pollUpdate,1500);}
  else{updateRestarting=false;byId('updateStatus').textContent=e.message+' If the app did not restart, load LZ4-Game-Compressor.elf in Payload Manager.';byId('checkUpdate').disabled=false;}
 }
}
async function checkUpdate(){try{await api('/api/gc/ampr-lz4/update/check','POST');await pollUpdate();}catch(e){byId('updateStatus').textContent=e.message;}}
byId('updates').onclick=()=>{tvOpen('updateDialog');pollUpdate();};
byId('checkUpdate').onclick=checkUpdate;
byId('installUpdate').onclick=async()=>{
 byId('installUpdate').disabled=true;
 try{await api('/api/gc/ampr-lz4/update/install','POST');await pollUpdate();}catch(e){byId('updateStatus').textContent=e.message;byId('installUpdate').disabled=false;}
};
// Read-only checks never install an update without the user's button press.
setTimeout(async()=>{try{const s=await api('/api/gc/ampr-lz4/status');if(!s.active)await checkUpdate();}catch(e){}},10000);
