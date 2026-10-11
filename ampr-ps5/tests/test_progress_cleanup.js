// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const assets=path.join(__dirname,'../assets');
const {progressSummary}=require(path.join(assets,'ampr-progress.js'));
const state={active:true,phase:'Packing game files',progressUnit:'bytes',done:64*1048576,total:128*1048576,stageElapsedSeconds:2};
assert.match(progressSummary(state).numbers,/32\.0 MiB\/s/);
assert.doesNotMatch(progressSummary(state).numbers,/files/);
assert.match(progressSummary({...state,phase:'Planning file rules and sampling',progressUnit:'files',done:2,total:47}).numbers,/2 files \/ 47 files/);
assert.doesNotMatch(progressSummary({...state,progressUnit:'files'}).numbers,/files\/s|MiB\/s/);

async function fixture(fail=false){
  const elements=new Map(),requests=[];let closed=0,deleting;
  const byId=id=>{if(!elements.has(id))elements.set(id,{disabled:false,value:'',textContent:'',checked:false,add(){},replaceChildren(){},addEventListener(){}});return elements.get(id);};
  const context={document:{getElementById:byId,querySelector(){return null;}},Option:class{},URLSearchParams,AbortSignal,Error,setTimeout(){},clearTimeout(){},tvWasActive:false,tvBenchmarkActive:false,tvSetStorage(){},tvClose(){closed++;},console,
    fetch:async(url,options)=>{requests.push([url,options.method]);if(url.endsWith('/delete-partial')){await new Promise(r=>{deleting=r;});return {ok:!fail,status:fail?400:200,json:async()=>fail?{ok:false,error:'Folder ownership marker changed'}:{ok:true}};}return {ok:true,json:async()=>url.endsWith('/status')?{ok:true,active:false,partialFolders:[]}:{ok:true,devices:[]}};}};
  vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(assets,'ampr-studio.js'),'utf8'),context);
  const pending=byId('deletePartial').onclick();
  assert.equal(byId('deletePartial').disabled,true);assert.equal(byId('keepPartial').disabled,true);
  assert.match(byId('cleanupStatus').textContent,/Deleting/);
  await byId('deletePartial').onclick();
  assert.equal(requests.filter(([u])=>u.endsWith('/delete-partial')).length,1);
  deleting();await pending;
  assert.equal(requests.find(([u])=>u.endsWith('/delete-partial'))[1],'POST');
  assert.equal(byId('deletePartial').disabled,false);
  if(fail){assert.equal(closed,0);assert.match(byId('cleanupStatus').textContent,/ownership marker changed/);}
  else{assert.equal(closed,1);assert.match(byId('phase').textContent,/Partial output deleted/);}
  const deletes=requests.filter(([u])=>u.endsWith('/delete-partial')).length;
  byId('keepPartial').onclick();assert.equal(requests.filter(([u])=>u.endsWith('/delete-partial')).length,deletes);
}
(async()=>{await fixture();await fixture(true);console.log('PASS: byte throughput, file-count planning, cleanup POST, busy controls, success, refusal and keep action');})().catch(error=>{console.error(error);process.exitCode=1;});
