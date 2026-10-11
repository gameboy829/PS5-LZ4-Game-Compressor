// SPDX-License-Identifier: GPL-3.0-or-later
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const source=fs.readFileSync(__dirname+'/../assets/ampr-tv.js','utf8');
const elements={output:{value:''},destinationStorage:{value:'/data/homebrew',options:[{value:'/data/homebrew'},{value:'/mnt/ext1/games'},{value:'/mnt/usb0'}]}};
const context={byId:id=>elements[id]};vm.createContext(context);
vm.runInContext(source.slice(source.indexOf('function syncDestinationFromFolder()'),source.indexOf('function sourceDrive()')),context);
for(const [path,drive] of [['/mnt/ext1/homebrew/PPSA12345-app_AMPR','/mnt/ext1/games'],['/mnt/ext1/games/PPSA12345-app_AMPR','/mnt/ext1/games'],['/mnt/ext1/etaHEN/games/PPSA12345-app_AMPR','/mnt/ext1/games'],['/mnt/usb0/Test','/mnt/usb0'],['/data/homebrew/Test','/data/homebrew']]){
 elements.output.value=path;context.syncDestinationFromFolder();assert.equal(elements.destinationStorage.value,drive);assert.equal(elements.output.value,path);
}
elements.output.value='/mnt/ext1/homebrew';assert.throws(()=>context.syncDestinationFromFolder(),/new game folder/);
elements.destinationStorage.options=[{value:'/data/homebrew'}];elements.output.value='/mnt/ext1/homebrew/Test';assert.throws(()=>context.syncDestinationFromFolder(),/unavailable/);
console.log('PASS: destination path selects M.2, internal and USB without resetting folder; unavailable drive and storage root rejected');
