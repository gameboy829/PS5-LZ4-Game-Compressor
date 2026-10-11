'use strict';

const byId=id=>document.getElementById(id);let games=[];

for(let level=1;level<=12;level++){const option=new Option(String(level),String(level));option.selected=level===7;byId('level').add(option);}

async function api(path,method='GET',timeout=30000){const response=await fetch(path,{cache:'no-store',method,signal:AbortSignal.timeout(timeout)});const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||`HTTP ${response.status}`);return result;}

function selected(){return games[Number(byId('game').value)];}

byId('refresh').onclick=async()=>{try{const previous=selected();folderLoadFailure='';const result=await api('/api/gc/games');games=result.games||[];byId('game').replaceChildren();games.forEach((game,index)=>byId('game').add(new Option(game.name+" — "+(game.sourceType==='folder'?'Folder Dump':'Image')+" — "+game.sourceStorage,String(index))));const previousIndex=previous?games.findIndex(g=>g.sourcePath===previous.sourcePath):-1;if(previousIndex>=0)byId('game').value=String(previousIndex);byId('game').onchange();}catch(error){byId('log').textContent=`Discovery failed: ${error.message}`;}};

byId('game').onchange=()=>{const game=selected();byId('source').textContent=game?`${game.sourceType==='folder'?'Original folder':'Original image'} · ${game.titleId} · ${game.sourceStorage} · ${game.sourcePath}`:'No mounted game found';if(game){byId('output').value=`${byId('destinationStorage').value||'/data/homebrew'}/${game.titleId}-app_AMPR`;byId('internal').value=`/data/homebrew/${game.titleId}-app_AMPR`;}mountAppearance(game&&game.readOnlyVerified);tvDestinationChanged();queueRequirements();measureGame();};

byId('mode').onchange=()=>{byId('internal').disabled=byId('mode').value!=='usb_then_internal';byId('game').onchange();};

function query(){const game=selected();if(!game)throw new Error('Select a discovered game first.');return new URLSearchParams({titleId:game.titleId,sourcePath:game.sourcePath,mode:byId('mode').value,output:byId('output').value,internal:byId('internal').value,level:byId('level').value,restartShadowMount:byId('restartShadowMount').checked?'yes':'no',verifyOutput:byId('verifyOutput').checked?'yes':'no',verifyCopy:byId('verifyCopy').checked?'yes':'no',originalAction:byId('originalAction').value,originalBackup:['move','swap'].includes(byId('originalAction').value)?originalBackupPath():''});}

for(const id of ['output','internal','level'])byId(id).addEventListener('input',queueRequirements);

let requirementsTimer=0,requirementsToken=0,requirementsBusy=false,requirementsMessage='Select a game and mount it before starting.';
let folderLoadPromise=null,folderLoadFailure='';
function mountAppearance(mounted){
    const folder=selected()?.sourceType==='folder';
    byId('mount').classList.toggle('mounted',!!mounted&&!folder);
    byId('mount').classList.toggle('folder-source',folder);
    byId('mount').disabled=folder||!!folderLoadPromise||tvWasActive||tvBenchmarkActive||startingConversion;
    byId('mount').textContent=folder?(mounted?'Folder Dump Loaded':'Loading Folder Dump…'):(mounted?'Mounted':'Mount game');
    byId('mountState').textContent=folder?(mounted?'Folder dump ready.':'Preparing folder dump…'):(mounted?'Game mounted.':'Mount the selected game to begin.');
}
async function loadFolderSource(source){
    if(folderLoadPromise)await folderLoadPromise;
    if(selected()?.sourcePath!==source.sourcePath)return;
    if(folderLoadFailure===source.sourcePath)throw new Error('Folder dump could not be loaded. Refresh games to retry.');
    folderLoadPromise=(async()=>{
        const status=await api('/api/gc/ampr-lz4/status');
        if(status.active||tvWasActive||tvBenchmarkActive||startingConversion)throw new Error('Wait for the current job to finish before loading a folder dump.');
        if(selected()?.sourcePath!==source.sourcePath)return;
        const listing=await api('/api/gc/games');
        if(listing.games.some(g=>g.sourcePath===source.sourcePath&&g.readOnlyVerified))return;
        await api('/api/gc/ampr-lz4/mount?'+new URLSearchParams({titleId:source.titleId,sourcePath:source.sourcePath}),'POST');
    })();
    try{await folderLoadPromise;}catch(error){folderLoadFailure=source.sourcePath;throw error;}
    finally{folderLoadPromise=null;}
}
function queueRequirements(){clearTimeout(requirementsTimer);++requirementsToken;requirementsMessage='Checking requirements...';byId('startRequirements').textContent='';if(!tvWasActive&&!tvBenchmarkActive&&!startingConversion)byId('convert').disabled=false;requirementsTimer=setTimeout(()=>checkRequirements(),350);}
async function checkRequirements(showMessage=false){
    clearTimeout(requirementsTimer);const token=++requirementsToken;requirementsBusy=true;
    try{
        if(!selected())throw new Error('Select a game first.');
        if(!byId('destinationStorage').value)throw new Error('Connect writable storage for the compressed game.');
        const source=selected();const listing=await api('/api/gc/games');if(token!==requirementsToken)return false;const current=listing.games.find(g=>g.titleId===source.titleId&&g.sourcePath===source.sourcePath);mountAppearance(current&&current.readOnlyVerified);
        if(source.sourceType==='folder'&&!current?.readOnlyVerified){await loadFolderSource(source);if(token!==requirementsToken)return false;}
        const result=await api('/api/gc/ampr-lz4/plan?'+query());if(token!==requirementsToken)return false;
        mountAppearance(result.readOnlyVerified);
        if(!result.readOnlyVerified)requirementsMessage=source.sourceType==='folder'?'Folder dump could not be verified read-only. Refresh games to retry.':'Mount the selected game before starting.';
        else if(!result.canStart)requirementsMessage=result.message||'Not enough space on the selected storage.';
        else{requirementsMessage='';byId('gameSize').textContent='Game: '+sizeText(result.logicalBytes)+' · Space needed: '+sizeText(result.requiredOutputBytes);}
        if(showMessage||byId('startRequirements').textContent)byId('startRequirements').textContent=requirementsMessage;
        return result.canStart;
    }catch(e){if(token===requirementsToken){requirementsMessage=e.message;if(selected()?.sourceType==='folder'){byId('mount').textContent='Folder Dump Not Loaded';byId('mountState').textContent=e.message;}if(showMessage||byId('startRequirements').textContent)byId('startRequirements').textContent=requirementsMessage;}return false;}
    finally{if(token===requirementsToken)requirementsBusy=false;}
}
byId('plan').onclick=()=>checkRequirements(true);

let cancelRequested=false,startingConversion=false,cleanupBusy=false,cleanupPrompted='',cleanupNotice='';
let polling=false,pollTimer=0,lastIdleState='',lastStage='',lastDone=-1,lastMovement=Date.now();
function schedulePoll(delay){clearTimeout(pollTimer);pollTimer=setTimeout(poll,delay);}

function showProgress(state){if(state.benchmark){if(typeof tvBenchResume==='function')tvBenchResume();else setTimeout(()=>tvBenchResume(),500);return;}if(typeof tvProgress==='function')tvProgress(state);const summary=progressSummary(state);if(state.phase!==lastStage||state.done!==lastDone){lastStage=state.phase;lastDone=state.done;lastMovement=Date.now();}byId('jobDescription').textContent=summary.description;byId('jobPerformance').textContent=summary.performance;byId('jobNumbers').textContent=state.active?summary.numbers:'';byId('jobTime').textContent=summary.time+(state.active&&Date.now()-lastMovement>30000?' ... No measurable progress for 30s; checking console status.':'');byId('jobFile').textContent=state.currentFile?`Current file: ${state.currentFile}`:'';byId('jobDestination').textContent=state.destination?`Saving to: ${state.destination}`:'';}

async function poll(){if(polling)return;clearTimeout(pollTimer);polling=true;try{const state=await api('/api/gc/ampr-lz4/status');showProgress(state);if(state.benchmark){schedulePoll(1500);return;}byId('partialPaths').textContent=(state.partialFolders||[]).join('\n');if(!state.active){cancelRequested=false;const key=(state.partialFolders||[]).join('\n');if(key&&key!==cleanupPrompted&&!cleanupBusy&&!document.querySelector('dialog[open]')){cleanupPrompted=key;byId('cleanupStatus').textContent='';tvOpen('cleanupDialog');}}byId('phase').textContent=state.active?`${state.phase || 'Preparing'}${state.total?` · ${state.stagePercent.toFixed(1)}%`:''}`:state.result?`Failed: ${state.error}`:state.started?(state.verificationSkipped?'Complete: unverified output — checks skipped':'Complete: output verified')+(state.originalStatus?' · Original: '+state.originalStatus:'')+(state.shadowMountStatus?' · ShadowMount: '+state.shadowMountStatus:''):'Ready';if(!state.active&&cleanupNotice)byId('phase').textContent=cleanupNotice;byId('progress').value=state.active?state.stagePercent:state.result||!state.started?0:100;byId('cancel').disabled=!state.active;

    const response=await fetch('/api/gc/ampr-lz4/log',{cache:'no-store'});if(response.ok)byId('log').textContent=await response.text();if(state.active){lastIdleState='';schedulePoll(1500);}else{const idleKey=JSON.stringify([state.started,state.result,state.phase,state.partialFolders]);schedulePoll(2000);if(idleKey===lastIdleState)return;lastIdleState=idleKey;startingConversion=false;mountAppearance(selected()?.readOnlyVerified);refreshSpace();byId('plan').disabled=false;byId('refresh').disabled=false;for(const id of ['destinationStorage','originalAction','verifyOutput','verifyCopy','restartShadowMount','game','mode','output','internal','level','paths','threads','benchmark','options'])byId(id).disabled=false;updateConversionOptions(false);queueRequirements();}

  }catch(error){byId('phase').textContent=`Connection interrupted: ${error.message}. The console job may still be running.`;schedulePoll(3000);}finally{polling=false;}}

async function startConversion(deleteConfirmed=false){if(tvWasActive||tvBenchmarkActive||startingConversion)return;if(requirementsBusy){byId('startRequirements').textContent='Checking the game and available space. Try again when the check finishes.';return;}byId('startRequirements').textContent='Checking requirements...';if(!await checkRequirements(true))return;byId('startRequirements').textContent='';if(byId('originalAction').value==='delete'&&!deleteConfirmed){const skipped=!byId('verifyOutput').checked||(byId('mode').value==='usb_then_internal'&&!byId('verifyCopy').checked);byId('originalDeleteWarning').textContent=skipped?'WARNING: You are skipping verification and deleting the original. Undetected corruption or an unbootable result could leave you without a usable game copy.':'The original will be deleted only after conversion and the selected checks finish. Keep a backup if you may need to restore it.';tvOpen('originalDeleteDialog');return;} cleanupNotice='';startingConversion=true;byId('phase').textContent='Starting conversion...';byId('jobDescription').textContent='Checking the source and preparing the worker.';byId('jobTime').textContent='Estimating time remaining...';byId('convert').disabled=true;if(typeof tvSwitch==='function')tvSwitch('progress');try{const request=query();if(deleteConfirmed){request.set('confirmOriginalDelete','yes');request.set('confirmUnverifiedDelete','yes');}await api(`/api/gc/ampr-lz4/start?${request}`,'POST');byId('convert').disabled=true;byId('plan').disabled=true;byId('refresh').disabled=true;for(const id of ['destinationStorage','originalAction','verifyOutput','verifyCopy','restartShadowMount','game','mode','output','internal','level','benchmark','threads','paths','options'])byId(id).disabled=true;byId('cancel').disabled=false;byId('mount').disabled=true;schedulePoll(500);}catch(error){startingConversion=false;byId('phase').textContent=`Start request interrupted: ${error.message}. Checking whether the console started the job...`;schedulePoll(500);}}
byId('convert').onclick=()=>startConversion();byId('cancelOriginalDelete').onclick=()=>tvClose();byId('confirmOriginalDelete').onclick=()=>{tvClose();startConversion(true);};

byId('keepPartial').onclick=()=>{if(!cleanupBusy){cleanupNotice='Canceled. Partial files kept.';tvClose();byId('phase').textContent=cleanupNotice;}};
byId('deletePartial').onclick=async()=>{
    if(cleanupBusy)return;cleanupBusy=true;
    byId('deletePartial').disabled=true;byId('keepPartial').disabled=true;
    byId('cleanupStatus').textContent='Deleting partial files… This can take a while.';
    try{
        await api('/api/gc/ampr-lz4/delete-partial','POST',120000);
        const state=await api('/api/gc/ampr-lz4/status');
        if(state.partialFolders?.length)throw new Error('Partial folders remain: '+state.partialFolders.join(', '));
        cleanupNotice='Canceled. Partial output deleted.';cleanupPrompted='';
        byId('partialPaths').textContent='';tvClose();byId('phase').textContent=cleanupNotice;
        refreshSpace();queueRequirements();
    }catch(error){
        cleanupNotice='Partial cleanup could not be confirmed. '+error.message;
        byId('cleanupStatus').textContent=cleanupNotice+' Retry after the console finishes cleanup.';
        byId('phase').textContent=cleanupNotice;
    }finally{cleanupBusy=false;byId('deletePartial').disabled=false;byId('keepPartial').disabled=false;}
};

byId('cancel').onclick=async()=>{try{await api('/api/gc/job/cancel','POST');cancelRequested=true;byId('phase').textContent='Cancelling after the current block...';byId('cancel').disabled=true;}catch(error){byId('phase').textContent=`Cancel failed: ${error.message}`;}};



function sizeText(bytes){return `${(bytes/1000000000).toFixed(1)} GB`;}

let sizeRequest=0;

async function measureGame(){const token=++sizeRequest;const game=selected();byId('gameSize').textContent=game?'Measuring game files...':'Select a game to measure its size.';if(!game)return;try{const result=await api(`/api/gc/ampr-lz4/size?${new URLSearchParams({titleId:game.titleId,sourcePath:game.sourcePath})}`);if(token!==sizeRequest)return;byId('gameSize').textContent=`Game: ${sizeText(result.logicalBytes)} · Space budget: ${sizeText(result.requiredOutputBytes)}`;}catch(error){if(token===sizeRequest)byId('gameSize').textContent=`Game size unavailable: ${error.message}`;}}

async function refreshSpace(){try{const result=await api('/api/gc/ampr-lz4/capacity');if(typeof tvSetStorage==='function'){tvSetStorage(result.devices);return;}const box=byId('storage');box.replaceChildren();for(const device of result.devices){const row=document.createElement('p');row.textContent=device.available?`${device.name} (${device.path}): ${sizeText(device.freeBytes)} free of ${sizeText(device.totalBytes)}`:`${device.name}: unavailable`;box.append(row);} }catch(error){byId('storage').textContent=`Storage unavailable: ${error.message}`;}}

byId('refreshSpace').onclick=refreshSpace;refreshSpace();



byId('mount').onclick=async()=>{const game=selected();if(game?.sourceType==='folder')return;if(!game){byId('mountState').textContent='Select a game first.';return;}byId('mount').disabled=true;mountAppearance(false);try{const result=await api('/api/gc/ampr-lz4/mount?'+new URLSearchParams({titleId:game.titleId,sourcePath:game.sourcePath}),'POST');byId('mountState').textContent='Mounting game...';for(let n=0;n<10;n++){await new Promise(resolve=>setTimeout(resolve,1000));const ready=await checkRequirements();if(byId('mount').classList.contains('mounted')){byId('mountState').textContent='Mounted read-only.';if(!ready)byId('startRequirements').textContent=requirementsMessage;return;}}byId('mountState').textContent='Mount could not be verified. Try mounting again.';}catch(e){byId('mountState').textContent='Mount failed: '+e.message;byId('startRequirements').textContent=byId('mountState').textContent;}finally{byId('mount').disabled=selected()?.sourceType==='folder';}};
