'use strict';
function duration(seconds){seconds=Math.max(0,Math.ceil(seconds));if(seconds<60)return `${seconds}s`;if(seconds<3600)return `${Math.floor(seconds/60)}m ${seconds%60}s`;return `${Math.floor(seconds/3600)}h ${Math.floor(seconds%3600/60)}m`;}
function progressSummary(state){
  const descriptions={
    'Returning converted game':'Returning the verified converted game to the original drive.',
    'Verifying returned game':'Checking the returned converted game byte for byte.',
    'Finishing swap':'Finishing the swap after verifying both games in their new locations.',
    'Restarting ShadowMount':'Restarting ShadowMount and refreshing the game library.',
    'Preparing runtime':'Preparing the files needed to run the compressed game.',
    'Building source index':'Listing game files. No full extracted copy is being created.',
    'Generating compression profile':'Choosing which files to compress or keep loose.',
    'Planning file rules and sampling':'Sampling files to choose compression settings.',
    'Packing game files':'Reading game data, compressing it with LZ4 and writing the archives.',
    'Copying loose files':'Copying files that must stay uncompressed. LZ4 packing is finished.',
    'Finalizing archives and runtime':'Finishing archive indexes, checksums and runtime files.',
    'Final verification':'Reading the output back and checking its decoded content against the original.',
    'Copying output to internal':'Copying the USB output to internal storage.',
    'Backing up original image':'Moving the original game to another drive: copying it to your selected Original Game Folder.',
    'Verifying original backup':'Checking the backup before removing the original from its old folder.',
    'Moving original image to backup':'Moving the original game into your selected Original Game Folder.',
    'Deleting original image':'Removing the original image as requested.',
    'Verifying internal copy':'Checking that the internal copy matches the USB output byte for byte.'
  };
  state={...state,done:Number(state.done)||0,total:Number(state.total)||0,performance:state.performance?{...state.performance,readMiBps:Number(state.performance.readMiBps)||0,writeMiBps:Number(state.performance.writeMiBps)||0,workerUtilization:Number(state.performance.workerUtilization)||0}:null};
  state={...state,done:Number(state.done)||0,total:Number(state.total)||0,performance:state.performance?{...state.performance,readMiBps:Number(state.performance.readMiBps)||0,writeMiBps:Number(state.performance.writeMiBps)||0,workerUtilization:Number(state.performance.workerUtilization)||0}:null};
  const bytes=state.progressUnit!=='files';const elapsed=state.stageElapsedSeconds||0;
  const rate=elapsed>0?state.done/elapsed:0;
  const amount=n=>bytes?`${(n/1073741824).toFixed(2)} GiB`:n.toLocaleString()+' files';
  let numbers=state.total?`${amount(state.done)} / ${amount(state.total)}${bytes&&rate>0?` · ${(rate/1048576).toFixed(1)} MiB/s`:''}`:(/original|ShadowMount/i.test(state.phase||'')?'Finishing this step…':'Discovering totals…');
  if(!state.active&&state.started)numbers=state.result?'Conversion stopped.':'All game files processed.';
  let performance='';if(state.active&&state.phase==='Copying loose files'&&state.performance){const p=state.performance;performance=`Read ${p.readMiBps.toFixed(1)} MiB/s · Write ${p.writeMiBps.toFixed(1)} MiB/s · ${p.readers||0} readers + ${p.writers||0} writers + 1 coordinator`;}else if(state.active&&state.performance?.workers){const p=state.performance;performance=`Read ${p.readMiBps.toFixed(1)} MiB/s · Write ${p.writeMiBps.toFixed(1)} MiB/s · ${p.readers?`${p.readers} readers · `:""}${p.workers} ${state.phase==='Packing game files'?'compressors':'workers'} (${p.workerUtilization.toFixed(0)}% busy)${p.writers?` · ${p.writers} writers`:""} · 1 coordinator`;}
  let estimate='Estimating time remaining…';
  if(!state.active)estimate=state.started?(state.result?'Stopped with an error.':state.verificationSkipped?'Verification skipped. Output is unverified.':'All selected checks finished.'):'Ready';
  else if(state.total&&state.done>=state.total)estimate='Finishing this stage…';
  else if(rate>0&&elapsed>=5&&state.total)estimate=`About ${duration((state.total-state.done)/rate)} remaining in this stage`;
  return {performance,description:!state.active&&state.started&&state.result?(state.phase==='Canceled'?'Conversion canceled.':'Conversion stopped. '+(state.error||'See diagnostics.')):!state.active&&state.started&&!state.result?(state.verificationSkipped?'Conversion finished. Output is unverified.':'Conversion finished and verified.'):(state.swapOriginal&&state.phase==='Copying output to internal'?'Returning the converted game to the original drive.':state.swapOriginal&&state.phase==='Verifying internal copy'?'Checking the returned converted game byte for byte.':descriptions[state.phase]||'Preparing the conversion.'),numbers,time:`Elapsed: ${duration(state.elapsedSeconds||0)} · ${estimate}`,percent:Math.max(0,Math.min(100,state.stagePercent||0))};
}
if(typeof module!=='undefined')module.exports={duration,progressSummary};
