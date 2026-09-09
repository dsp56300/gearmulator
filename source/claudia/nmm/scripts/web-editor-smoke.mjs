import {spawn} from 'node:child_process';
import {mkdtempSync,writeFileSync,createWriteStream,readFileSync,rmSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {resolve,dirname,join} from 'node:path';
import {tmpdir} from 'node:os';
import {createHash} from 'node:crypto';
// macOS development test, Node 22+. Download the public v0.5.2 bundle first:
// curl -L --fail https://nordmodulareditor.com/assets/index-CdD0yasZ.js -o /tmp/nmm-editor.js
// node source/claudia/nmm/scripts/web-editor-smoke.mjs firmware.bin FourVoices.pch /tmp/nmm-editor.js
// This opens a disposable Chrome profile and local virtual MIDI test service.
// The test uses Chrome's legacy MIDI backend; see analysis/editor-midi.md.
// No website source is redistributed. Hooks are pinned to this public bundle.
const [firmware,patch,bundlePath]=process.argv.slice(2);
if(!firmware||!patch||!bundlePath) throw Error('usage: web-editor-smoke.mjs firmware.bin FourVoices.pch downloaded-editor.js');
const bundle=readFileSync(bundlePath,'utf8');
if(createHash('sha256').update(bundle).digest('hex')!=='a8b9cc07080c5726f17c3f54ffaf6f4b57fa6e9c54667de5858cfd4d6627181a') throw Error('Public editor bundle changed; inspect and update test hooks before running');
const root=resolve(dirname(fileURLToPath(import.meta.url)),'../../../..');
const artifacts=mkdtempSync(join(tmpdir(),'nmm-browser-test-'));
console.log('Test artifacts:',artifacts);
const log=createWriteStream(join(artifacts,'service.log'));
const server=spawn(root+'/build/nmm-ui/source/claudia/nmm/nmmJucePlugin/nmmEditorMidiTests_artefacts/Release/nmmEditorMidiTests',[resolve(firmware),resolve(patch),'--serve']);
let output='';server.stdout.on('data',b=>{output+=b;log.write(b)});server.stderr.pipe(log);
const profile=mkdtempSync(join(tmpdir(),'nmm-editor-chrome-'));
const chrome=spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',['--disable-background-networking','--no-first-run','--no-default-browser-check','--disable-extensions','--disable-features=MidiMacUmp','--mute-audio','--remote-debugging-port=9228','--user-data-dir='+profile,'about:blank'],{stdio:['ignore','ignore','pipe']});
chrome.stderr.pipe(createWriteStream(join(artifacts,'chrome.log')));
const pause=ms=>new Promise(r=>setTimeout(r,ms));
let ws;let serial=0;const pending=new Map();
const call=(method,params={},sessionId)=>new Promise((resolve,reject)=>{const id=++serial;const timer=setTimeout(()=>{pending.delete(id);reject(Error('CDP timeout '+method))},60000);pending.set(id,{resolve:r=>{clearTimeout(timer);resolve(r)},reject:e=>{clearTimeout(timer);reject(e)}});ws.send(JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})}));});
try {
 for(let i=0;i<100&&!output.includes('READY ');i++) {if(server.exitCode!==null) throw Error('MIDI server exited '+server.exitCode);await pause(100)}
 if(!output.includes('READY ')) throw Error('MIDI server readiness timeout');
 let version;for(let i=0;i<100;i++){try{version=await (await fetch('http://127.0.0.1:9228/json/version')).json();break}catch{await pause(100)}}
 ws=new WebSocket(version.webSocketDebuggerUrl);await new Promise(r=>ws.addEventListener('open',r,{once:true}));
 ws.onmessage=async ev=>{const m=JSON.parse(ev.data);if(m.id){const p=pending.get(m.id);pending.delete(m.id);if(m.error)p?.reject(Error(JSON.stringify(m.error)));else p?.resolve(m.result);return;}
 if(m.method==='Runtime.exceptionThrown') console.error('PAGE ERROR',JSON.stringify(m.params));
 if(m.method==='Fetch.requestPaused') {console.log('intercept',m.params.request.url);
  try {const body=bundle+'\n;globalThis.__nmmTest={identity:AAe,settings:RAe,patch:eAe,send:Ki,input:Du,output:$w,upload:bRe,list:DAe,store:iRe,load:hRe,delete:cRe,selection:zM,mutex:Wv,inputDevice:()=>J4,traffic:Vu};';
   await call('Fetch.fulfillRequest',{requestId:m.params.requestId,responseCode:200,responseHeaders:[{name:'Content-Type',value:'text/javascript; charset=utf-8'}],body:Buffer.from(body).toString('base64')},m.sessionId);
  }catch(e){console.error('intercept',e)}
 }
 };
 await call('Browser.grantPermissions',{origin:'https://nordmodulareditor.com',permissions:['midi','midiSysex']});
 const target=await call('Target.createTarget',{url:'about:blank'});const {sessionId}=await call('Target.attachToTarget',{targetId:target.targetId,flatten:true});
 await call('Runtime.enable',{},sessionId);
 await call('Page.enable',{},sessionId);
 await call('Page.addScriptToEvaluateOnNewDocument',{source:'const originalMidiRequest=navigator.requestMIDIAccess.bind(navigator);navigator.requestMIDIAccess=(...args)=>originalMidiRequest(...args).then(access=>{globalThis.__nmmAccess=access;return access;});'},sessionId);
 await call('Fetch.enable',{patterns:[{urlPattern:'https://nordmodulareditor.com/assets/index-*.js',resourceType:'Script',requestStage:'Request'}]},sessionId);
 await call('Page.navigate',{url:'https://nordmodulareditor.com/g1-patch-editor'},sessionId);
 const evaluate=async expression=>{const r=await call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true},sessionId);if(r.exceptionDetails){const diag=await call('Runtime.evaluate',{expression:'JSON.stringify({stage:globalThis.__nmmStage,raw:globalThis.__nmmRaw,traffic:__nmmTest.traffic.getState(),connection:__nmmTest.inputDevice()?.connection})',returnByValue:true},sessionId);writeFileSync(join(artifacts,'diagnostic.json'),diag.result.value||JSON.stringify(diag.result));console.log('DIAGNOSTICS',diag.result);throw Error(JSON.stringify(r.exceptionDetails));}return r.result.value;};
 for(let i=0;i<100;i++){if(await evaluate('typeof __nmmTest === "object"'))break;await pause(300)}
 console.log('HOOKS',await evaluate('typeof __nmmTest'));

 for(let i=0;i<100;i++){if(await evaluate('!!globalThis.__nmmAccess'))break;await pause(100)}
 const portName=output.match(/READY (.+)/)[1].trim();console.log('PORT',portName);
 console.log('BROWSER PORTS',await evaluate(`(async()=>{const m=globalThis.__nmmAccess;return {inputs:[...m.inputs.values()].map(p=>p.name),outputs:[...m.outputs.values()].map(p=>p.name)}})()`));
 const result=await evaluate(`(async()=>{const midi=globalThis.__nmmAccess;const input=[...midi.inputs.values()].find(p=>p.name===${JSON.stringify(portName+' PC Out')});const output=[...midi.outputs.values()].find(p=>p.name===${JSON.stringify(portName+' PC In')});if(!input||!output)throw Error('Browser did not see virtual ports');__nmmTest.selection.getState().setSelectedInput(input.id);__nmmTest.selection.getState().setSelectedOutput(output.id);await new Promise(r=>setTimeout(r,1200));globalThis.__nmmRaw=[];input.addEventListener('midimessage',e=>__nmmRaw.push([...e.data]));await input.open();console.log('OPEN',midi.sysexEnabled,input.connection);return __nmmTest.mutex.runExclusive(async()=>{globalThis.__nmmStage='identity';const identity=await __nmmTest.identity();globalThis.__nmmStage='settings';const settings=await __nmmTest.settings();globalThis.__nmmStage='patch read';let patch=await __nmmTest.patch({slot:0});__nmmTest.send({type:'Parameter',subCommand:'ParameterChange',slotForCommandByte:0,pid:patch.pid,section:1,module:4,parameter:0,value:80});await new Promise(r=>setTimeout(r,300));patch=await __nmmTest.patch({slot:0});globalThis.__nmmStage='upload';await __nmmTest.upload({slot:0,patch,patchName:'BrowserTest',chunkSizeBytes:56});globalThis.__nmmStage='read uploaded patch';const restored=await __nmmTest.patch({slot:0});globalThis.__nmmStage='initial list';const initialList=await __nmmTest.list(0);globalThis.__nmmStage='store 99';await __nmmTest.store({slot:0,bank:0,position:98});globalThis.__nmmStage='stored list';const storedList=await __nmmTest.list(0);globalThis.__nmmStage='load 2';await __nmmTest.load(0,0,1);globalThis.__nmmStage='load 99';await __nmmTest.load(0,0,98);globalThis.__nmmStage='read library patch';const fromLibrary=await __nmmTest.patch({slot:0});return {initialList,storedList,fromLibrary,input:input.name,output:output.name,identity,settings,editedParameters:patch.poly.parameterDumpSection,restored};});})()`);
 writeFileSync(join(artifacts,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({identity:result.identity,voices:result.restored.header.voices,name:result.restored.patchName2,pid:result.restored.pid}));
 const level=p=>p.poly.parameterDumpSection.parameters.find(m=>m.index===4)?.params.level;
 if(result.editedParameters.parameters.find(m=>m.index===4)?.params.level!==80||level(result.restored)!==80) throw Error('Live edit or upload lost output-level value');
 if(result.identity.device!=='Micro'||result.restored.header.voices!==4||result.restored.patchName2?.name!=='BrowserTest') throw Error('Browser protocol verification failed');
 if(result.initialList[0]?.isEmpty||result.initialList[1]?.name!=='Second patch'||result.storedList[98]?.name!=='BrowserTest'||result.fromLibrary.patchName2?.name!=='BrowserTest'||level(result.fromLibrary)!==80) throw Error('Browser library names/store/load verification failed');
 console.log('PASS Chrome Web MIDI: public editor discovery, settings, full patch retrieval, live parameter change, multipart upload, all 99 library positions, native store and reload');
} finally {if(ws?.readyState===1){ws.send(JSON.stringify({id:++serial,method:'Browser.close'}));ws.close()}chrome.kill();server.kill();await pause(500);rmSync(profile,{recursive:true,force:true});}
