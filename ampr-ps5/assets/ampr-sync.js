// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
let sharedReady=false,sharedRevision=-1,sharedApplying=false,sharedSending=null,sharedTimer=0,sharedDirty={};
const sharedFields=['destinationStorage','originalAction','output','internal','level','verifyOutput','verifyCopy','restartShadowMount','originalLocation','originalSubfolder'];
const sharedFlags=new Set(['verifyOutput','verifyCopy','restartShadowMount']);
function sharedValues(){const values={sourcePath:selected()?.sourcePath||''};for(const id of sharedFields)values[id]=sharedFlags.has(id)?byId(id).checked:byId(id).value;return values;}
function sharedChanged(id){
    if(!sharedReady||sharedApplying||tvWasActive||tvBenchmarkActive)return;
    const values=sharedValues();let keys=[id];
    if(id==='game')keys=['sourcePath','output','internal','originalAction'];
    if(id==='destinationStorage')keys=['destinationStorage','output','internal','originalAction','originalLocation','verifyOutput','verifyCopy'];
    if(id==='originalAction')keys=['originalAction','internal','originalLocation','verifyOutput','verifyCopy'];
    for(const key of keys)sharedDirty[key]=values[key];
    clearTimeout(sharedTimer);sharedTimer=setTimeout(()=>sharedFlush().catch(e=>byId('startRequirements').textContent='Settings sync failed: '+e.message),250);
}
async function sharedFlush(){
    clearTimeout(sharedTimer);if(sharedSending)await sharedSending;
    if(!Object.keys(sharedDirty).length)return;
    const patch=sharedDirty;sharedDirty={};
    sharedSending=api('/api/gc/ampr-lz4/setup?'+new URLSearchParams({settings:JSON.stringify(patch)}),'POST');
    try{const result=await sharedSending;sharedRevision=-1;return result;}
    catch(e){sharedDirty={...patch,...sharedDirty};throw e;}
    finally{sharedSending=null;}
}
function sharedApply(settings){
    if(sharedSending||Object.keys(sharedDirty).length)return;
    sharedApplying=true;
    try{
        const oldPath=selected()?.sourcePath;const index=games.findIndex(g=>g.sourcePath===settings.sourcePath);
        if(index>=0)byId('game').value=String(index);
        for(const id of sharedFields)if(settings[id]!==undefined){if(sharedFlags.has(id))byId(id).checked=settings[id];else byId(id).value=settings[id];}
        const game=selected();byId('source').textContent=game?`${game.sourceType==='folder'?'Original folder':'Original image'} · ${game.titleId} · ${game.sourceStorage} · ${game.sourcePath}`:'Select a game';
        tvDestinationChanged(false);updateOriginalMovePath();mountAppearance(game?.readOnlyVerified);
        if(!tvWasActive&&!tvBenchmarkActive){queueRequirements();if(oldPath!==game?.sourcePath)measureGame();}
    }finally{sharedApplying=false;}
}
async function sharedInitialize(){
    try{
        await refreshSpace();const result=await api('/api/gc/ampr-lz4/setup');
        sharedReady=true;
        if(Object.keys(result.settings).length){sharedApply(result.settings);sharedRevision=result.revision;}
        else{sharedDirty=sharedValues();await sharedFlush();}
        queueRequirements();
    }catch(e){sharedReady=true;byId('startRequirements').textContent='Settings sync unavailable: '+e.message;}
}
async function sharedPoll(state){
    try{
        const result=await api('/api/gc/ampr-lz4/setup');
        if(result.settings.sourcePath&&!games.some(g=>g.sourcePath===result.settings.sourcePath)){const listing=await api('/api/gc/games');games=listing.games||[];byId('game').replaceChildren();games.forEach((game,index)=>byId('game').add(new Option(game.name+' — '+(game.sourceType==='folder'?'Folder Dump':'Image')+' — '+game.sourceStorage,String(index))));sharedRevision=-1;}
        if(result.revision!==sharedRevision&&!sharedSending&&!Object.keys(sharedDirty).length){sharedApply(result.settings);sharedRevision=result.revision;}
        if(state.started&&state.sourcePath){const index=games.findIndex(g=>g.sourcePath===state.sourcePath);if(state.active&&index>=0)byId('game').value=String(index);}
        if(!state.active){
            const listing=await api('/api/gc/games');const game=selected();
            if(game){const current=listing.games.find(g=>g.sourcePath===game.sourcePath);if(current){const changed=game.readOnlyVerified!==current.readOnlyVerified;game.readOnlyVerified=current.readOnlyVerified;mountAppearance(current.readOnlyVerified);if(changed)queueRequirements();}}
        }
    }catch(e){byId('startRequirements').textContent='Settings sync interrupted: '+e.message;}
}
for(const id of ['game',...sharedFields])byId(id).addEventListener(sharedFlags.has(id)||!['output','internal','originalSubfolder'].includes(id)?'change':'input',()=>sharedChanged(id));
byId('savePaths').addEventListener('click',()=>{for(const id of ['output','internal','destinationStorage','originalLocation','originalSubfolder'])sharedChanged(id);});
