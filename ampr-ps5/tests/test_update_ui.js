// SPDX-License-Identifier: GPL-3.0-or-later
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const elements=new Map();const byId=id=>{if(!elements.has(id))elements.set(id,{});return elements.get(id);};
let response={currentVersion:'beta.2',phase:'ready',available:true,busy:false,release:{size:100},message:'Update available'},reloads=0;
const c={byId,setTimeout:()=>1,clearTimeout(){},Date,location:{reload(){reloads++;}},tvOpen(){},api:async()=>response};vm.createContext(c);vm.runInContext(fs.readFileSync(__dirname+'/../assets/ampr-update.js','utf8'),c);
(async()=>{await c.pollUpdate();assert.equal(byId('updates').textContent,'Update available');assert.equal(byId('installUpdate').disabled,false);
 response={...response,busy:true,phase:'downloading',downloadedBytes:50};await c.pollUpdate();assert.equal(byId('installUpdate').disabled,true);assert.equal(byId('updateProgress').value,50);
 response={...response,phase:'restarting'};await c.pollUpdate();
 response={...response,busy:false,phase:'idle',lastResult:{phase:'error',message:'Previous build restored'}};await c.pollUpdate();assert.equal(byId('checkUpdate').disabled,false);assert.match(byId('updateStatus').textContent,/Previous build restored/);
 response={...response,phase:'restarting',lastResult:undefined};await c.pollUpdate();response={...response,currentVersion:'beta.3',busy:false,phase:'idle'};await c.pollUpdate();assert.equal(reloads,1);
 console.log('PASS: availability, download progress, busy controls and restart reconnect');})().catch(e=>{console.error(e);process.exitCode=1;});
