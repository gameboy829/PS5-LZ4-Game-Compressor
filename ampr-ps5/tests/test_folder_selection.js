const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const elements=new Map(),requests=[];let active=false,mounted=false;
const folder={name:'Test',titleId:'PPSA12345',sourcePath:'/mnt/ext1/games/Test',sourceType:'folder',sourceStorage:'M.2 SSD'};
const el=id=>{if(!elements.has(id))elements.set(id,{value:'',disabled:false,textContent:'',classList:{toggle(){}},add(){},replaceChildren(){},addEventListener(){}});return elements.get(id);};
const context={document:{getElementById:el},Option:class{},URLSearchParams,AbortSignal,setTimeout(){},clearTimeout(){},tvWasActive:false,tvBenchmarkActive:false,tvSetStorage(){},fetch:async(url)=>{requests.push(url);if(url.includes('/mount?'))mounted=true;return {ok:true,json:async()=>url.endsWith('/status')?{ok:true,active}:url.endsWith('/games')?{ok:true,games:[{...folder,readOnlyVerified:mounted}]}:{ok:true,devices:[]}};}};
vm.createContext(context);vm.runInContext(fs.readFileSync(__dirname+'/../assets/ampr-studio.js','utf8'),context);context.folder=folder;
(async()=>{
 vm.runInContext('games=[folder];',context);el('game').value='0';
 await vm.runInContext('loadFolderSource(selected())',context);
 vm.runInContext('mountAppearance(true)',context);
 assert.equal(el('mount').disabled,true);assert.equal(el('mount').textContent,'Folder Dump Loaded');
 await vm.runInContext('loadFolderSource(selected())',context);assert.equal(requests.filter(x=>x.includes('/mount?')).length,1);
 mounted=false;active=true;await assert.rejects(vm.runInContext('loadFolderSource(selected())',context),/current job/);
 assert.equal(requests.filter(x=>x.includes('/mount?')).length,1);
 vm.runInContext("games=[{...folder,sourceType:'image'}];mountAppearance(false)",context);
 assert.equal(el('mount').disabled,false);assert.equal(el('mount').textContent,'Mount game');
 console.log('PASS: automatic folder load, disabled loaded state, duplicate suppression, active job guard, image mount enabled');
})().catch(e=>{console.error(e);process.exitCode=1;});
