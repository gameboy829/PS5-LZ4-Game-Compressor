// SPDX-License-Identifier: GPL-3.0-or-later
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const text=fs.readFileSync(__dirname+'/../assets/ampr-studio.js','utf8');
const elements=new Map(),timers=[];let popups=0;let status={ok:true,active:false,started:false,phase:'Ready',partialFolders:[]},refreshes=0;
const el=id=>{if(!elements.has(id))elements.set(id,{});return elements.get(id);};
const c={byId:el,setTimeout:(fn,delay)=>{timers.push([fn,delay]);return timers.length;},clearTimeout(){},Date,JSON,api:async()=>status,showProgress(){},document:{querySelector(){return null;}},fetch:async()=>({ok:true,text:async()=>''}),selected:()=>null,mountAppearance(){},refreshSpace(){refreshes++;},updateConversionOptions(){},queueRequirements(){},tvOpen(){popups++;},startingConversion:false,cancelRequested:false,cleanupBusy:false,cleanupPrompted:'',cleanupNotice:''};vm.createContext(c);
vm.runInContext(text.slice(text.indexOf('let polling=false'),text.indexOf('function showProgress')),c);
vm.runInContext(text.slice(text.indexOf('async function poll()'),text.indexOf('async function startConversion')),c);
(async()=>{
 await c.poll();assert.equal(timers.at(-1)[1],2000);assert.equal(refreshes,1);
 await c.poll();assert.equal(refreshes,1,'idle heartbeat must not reset setup');
 status={...status,active:true,started:true,phase:'Packing game files',total:100,done:30,stagePercent:30};await c.poll();assert.match(el('phase').textContent,/Packing/);assert.equal(timers.at(-1)[1],1500);assert.equal(el('cancel').disabled,false);
 status={...status,active:false,phase:'Complete',done:100};await c.poll();assert.match(el('phase').textContent,/Complete/);assert.equal(el('cancel').disabled,true);assert.equal(refreshes,2);assert.equal(timers.at(-1)[1],2000);
 assert.equal(popups,1,'completion popup opens once');
 await c.poll();assert.equal(popups,1,'continued polling does not reopen popup');
 status={...status,result:-1,jobId:'failed'};await c.poll();assert.equal(popups,1,'failed jobs do not show success');
 status={...status,result:0,jobId:'next',elapsedSeconds:5460,gameName:'Test game',destination:'/test'};await c.poll();assert.equal(popups,2);assert.match(el('completionMessage').textContent,/1h 31m 0s/);
 console.log('PASS: idle browser continues polling, sees remotely started job and completion, avoids repeated setup reset');
})().catch(e=>{console.error(e);process.exitCode=1;});
