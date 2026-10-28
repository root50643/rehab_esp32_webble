'use strict';
const http=require('node:http'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {chromium}=require('playwright');
const root=path.resolve(__dirname,'../web');
const server=http.createServer((req,res)=>{const name=req.url==='/'?'index.html':req.url.slice(1);if(!['index.html','app.js','protocol.js','style.css'].includes(name)){res.writeHead(404);res.end();return;}res.setHeader('Content-Type',name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html');res.end(fs.readFileSync(path.join(root,name)));});
(async()=>{
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_PATH?{executablePath:process.env.CHROMIUM_PATH}:{})});
  try{
    const page=await browser.newPage();const errors=[];page.on('pageerror',error=>errors.push(error.message));
    await page.addInitScript(()=>{
      let owner=false;const chars=new Map();const writes=[];
      class Characteristic extends EventTarget {
        async startNotifications(){return this;}
        async readValue(){return new DataView(new TextEncoder().encode(JSON.stringify({v:1,protocol:1,peer_key:1})).buffer);}
        async writeValueWithResponse(frame){
          if(window.bleMock?.hold){window.bleMock.hold=false;return new Promise(resolve=>{window.bleMock.releaseOld=resolve;});}
          const P=window.RehabProtocol;this.assembler??=new P.FrameReassembler(3,1024);const data=this.assembler.push(frame);if(!data)return;
          const cmd=P.decodeJson(data);writes.push(cmd);
          if(cmd.cmd==='claim')owner=true;if(cmd.cmd==='release')owner=false;
          const lease={owner:owner?1:null,remaining_ms:owner?30000:0,you_control:owner,stopping:false};
          const response={v:1,type:'response',id:cmd.id,cmd:cmd.cmd,ok:true,lease};
          setTimeout(()=>{emit('response',response);if(cmd.cmd==='get_state')state();},0);
        }
      }
      function emit(kind,value){const P=window.RehabProtocol,ch=chars.get(P.UUID[kind]);for(const frame of P.chunkPayload(P.encodeJson(value),20,5,P.CHANNEL[kind])){ch.value=new DataView(frame.buffer);ch.dispatchEvent(new Event('characteristicvaluechanged'));}}
      function state(){emit('state',{v:1,type:'state',seq:1,uptime_ms:1,heart:{bpm:82,age_ms:1},machine:{state:'Start',rpm:300,reported_level:7,torque_raw:250,age28_ms:1,age3f_ms:1},rfid:{card_present:true,uid:'00010203'},lease:{owner:owner?1:null,remaining_ms:30000,you_control:owner}});}
      const device=new EventTarget();device.name='RehabWebBLE test';device.gatt={connected:false,async connect(){this.connected=true;return{getPrimaryService:async()=>({getCharacteristic:async uuid=>{if(!chars.has(uuid))chars.set(uuid,new Characteristic());return chars.get(uuid);}})};},disconnect(){this.connected=false;owner=false;device.dispatchEvent(new Event('gattserverdisconnected'));}};
      Object.defineProperty(navigator,'bluetooth',{value:{requestDevice:async()=>device}});
      window.bleMock={writes,emit,state};
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/`);await page.click('#connect');await page.waitForFunction(()=>document.getElementById('bpm').textContent==='82');
    assert.equal(await page.textContent('#rpm'),'300');assert.equal(await page.textContent('#torque'),'250');assert.equal(await page.textContent('#cardUid'),'00010203');
    assert.equal(await page.locator('[data-command="start"]').isDisabled(),true);
    await page.click('#claim');await page.waitForFunction(()=>!document.querySelector('[data-command="start"]').disabled);
    await page.fill('#resistanceInput','300');await page.click('#setResistance');await page.waitForFunction(()=>window.bleMock.writes.some(c=>c.cmd==='set_resistance'&&c.level===300));
    await page.evaluate(()=>window.bleMock.emit('state',{v:1,type:'state',heart:{bpm:null,age_ms:10000},machine:{rpm:null,reported_level:null,torque_raw:null,age28_ms:5000,age3f_ms:5000},rfid:{card_present:false},lease:{owner:1,you_control:true,remaining_ms:25000}}));
    assert.equal(await page.textContent('#bpm'),'—');assert.equal(await page.textContent('#cardUid'),'—');
    await page.evaluate(()=>window.bleMock.state());
    for(const width of [360,768,1440]){await page.setViewportSize({width,height:950});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);}
    const evidence=path.resolve(__dirname,'../build/screenshots');fs.mkdirSync(evidence,{recursive:true});await page.screenshot({path:path.join(evidence,'webble-desktop.png'),fullPage:true});
    await page.evaluate(()=>{window.bleMock.hold=true;});await page.click('[data-command="start"]');await page.waitForFunction(()=>!!window.bleMock.releaseOld);
    await page.click('#disconnect');assert.equal(await page.locator('[data-command="start"]').isDisabled(),true);assert.equal(await page.textContent('#bpm'),'—');
    await page.click('#reconnect');await page.waitForFunction(()=>document.getElementById('bpm').textContent==='82');
    await page.evaluate(()=>window.bleMock.releaseOld());
    assert.equal(await page.locator('[data-command="start"]').isDisabled(),true);
    assert.deepEqual(errors,[]);console.log('Browser BLE mock, notifications, lease UI, writes, stale values, reconnect and RWD passed.');
  }finally{await browser.close();server.close();}
})().catch(error=>{console.error(error);server.close();process.exitCode=1;});
