'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),P=require('../web/protocol.js');
test('UUIDs match the deployed firmware',()=>{const source=fs.readFileSync('firmware/RehabEsp32/src/gateway_service.cpp','utf8');for(const uuid of Object.values(P.UUID))assert.ok(source.includes(uuid));});
test('UTF-8 and DataView offsets survive fragmentation at default MTU',()=>{
  const value={v:1,id:17,cmd:'claim',text:'心率'},wire=P.encodeJson(value),frames=P.chunkPayload(wire,20,0x1234,P.CHANNEL.command),a=new P.FrameReassembler(3);let result;
  for(const frame of frames){assert.ok(frame.length<=20);const padded=new Uint8Array(frame.length+8);padded.set(frame,4);result=a.push(new DataView(padded.buffer,4,frame.length),100);}
  assert.deepEqual(P.decodeJson(result),value);assert.equal(frames[0][0],0xa7);assert.equal(frames[0][4],0x34);assert.equal(frames[0][5],0x12);
});
test('duplicates, out-of-order, truncated, wrong channel and expired fragments fail',()=>{
  const f=P.chunkPayload(P.encodeJson({v:1,id:1,cmd:'set_resistance',level:300}),20,1,3);
  let a=new P.FrameReassembler(3);assert.equal(a.push(f[0],0),null);assert.throws(()=>a.push(f[0],1));
  a=new P.FrameReassembler(3);assert.throws(()=>a.push(f[1],0));assert.equal(a.push(f[0],0),null);assert.throws(()=>a.push(f[1],2000));
  assert.throws(()=>new P.FrameReassembler(1).push(f[0]));assert.throws(()=>a.push(new Uint8Array(4)));
});
test('size, request schema, 16-bit range and malformed UTF-8 are bounded',()=>{
  assert.throws(()=>P.chunkPayload(new Uint8Array(1025)));assert.throws(()=>P.decodeJson(new Uint8Array([0xc0,0x80])));
  assert.equal(P.command('set_resistance',1,{level:65535}).level,65535);
  for(const level of [-1,65536,1.5,NaN])assert.throws(()=>P.command('set_resistance',1,{level}));
  assert.throws(()=>P.command('claim',0));assert.throws(()=>P.command('raw_uart',1));
  assert.equal(P.command('set_heart_rate_address',1,{address:'aa:bb:cc:dd:ee:ff'}).address,'AA:BB:CC:DD:EE:FF');
});
test('zero is a real reading and freshness includes time since the last state',()=>{
  assert.equal(P.sample(0,0,0,5000,0),'0');assert.equal(P.sample(50,4900,0,5000,99),'50');assert.equal(P.sample(50,4900,0,5000,100),'—');assert.equal(P.sample(null,0,0,5000,0),'—');
});
test('static client uses local assets and safe rendering',()=>{
  const html=fs.readFileSync('web/index.html','utf8'),js=fs.readFileSync('web/app.js','utf8');
  assert.match(html,/lang="zh-Hant"/);assert.doesNotMatch(html,/(?:src|href)="https?:/);assert.doesNotMatch(js,/\.innerHTML|document\.write|insertAdjacentHTML/);assert.match(js,/writeValueWithResponse/);
});
