// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
let updateTimer=0,updateRestarting=false,updateRestartBegan=0,updateCurrent='';
function renderUpdate(s){
 updateCurrent=s.currentVersion;byId('updateVersion').textContent=`Installed version: ${s.currentVersion}`;
 const bytes=s.downloadedBytes||0,total=s.release?.size||0;
 let message=s.message||s.lastResult?.message||'Check for a new release.';
 if(s.phase==='checking')message='Checking GitHub for updates…';
 else if(s.phase==='downloading')message=total?`Downloading update: ${Math.min(100,bytes/total*100).toFixed(0)}% · ${(bytes/1048576).toFixed(1)} / ${(total/1048576).toFixed(1)} MiB`:'Downloading update…';
 else if(s.phase==='restarting')message='Update downloaded. Restarting the app and reconnecting…';
 else if(s.lastResult?.phase==='complete'&&!s.available&&!s.busy)message=`Update installed successfully. You are running ${s.currentVersion}.`;
 else if(s.phase==='ready'&&!s.available)message=`You are up to date (${s.currentVersion}).`;
 byId('updateStatus').textContent=message;
 byId('checkUpdate').disabled=Boolean(s.busy||updateRestarting);
 byId('installUpdate').disabled=Boolean(!s.available||s.busy||updateRestarting);
 byId('updateProgress').hidden=!s.busy;byId('updateProgress').value=total?Math.min(100,bytes/total*100):0;
 byId('updates').textContent=s.busy?'Updating…':s.available?'Update available':'Updates';byId('updates').className=s.available&&!s.busy?'small secondary update-alert':'small secondary';
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
setTimeout(async()=>{try{const s=await api('/api/gc/ampr-lz4/status');if(!s.active)await checkUpdate();}catch(e){}},1500);
async function backgroundUpdateCheck(){try{const state=await api('/api/gc/ampr-lz4/status');if(!state.active&&!updateRestarting)await checkUpdate();}catch(e){}finally{setTimeout(backgroundUpdateCheck,10*60*1000);}}
setTimeout(backgroundUpdateCheck,10*60*1000);
