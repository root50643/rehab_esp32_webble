(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.RehabProtocol = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';
  const UUID = Object.freeze(Object.fromEntries(['service','state','event','command','response','info'].map((name,i)=>[name,`7e40000${i+1}-b5a3-f393-e0a9-e50e24dcca9e`])));
  const CHANNEL=Object.freeze({state:1,event:2,command:3,response:4,info:5});
  const MAX_JSON=4096, MAX_COMMAND=1024, HEADER=8;
  const encoder=new TextEncoder(),decoder=new TextDecoder('utf-8',{fatal:true});
  function bytes(value){
    if(ArrayBuffer.isView(value))return new Uint8Array(value.buffer,value.byteOffset,value.byteLength);
    return new Uint8Array(value);
  }
  function encodeJson(value){return encoder.encode(JSON.stringify(value));}
  function decodeJson(value){const text=typeof value==='string'?value:decoder.decode(bytes(value));if(text.includes('\0'))throw new Error('NUL in JSON');return JSON.parse(text);}
  function chunkPayload(payload,capacity=20,messageId=1,channel=CHANNEL.command){
    const data=bytes(payload),limit=channel===CHANNEL.command?MAX_COMMAND:MAX_JSON;
    if(!data.length||data.length>limit||capacity<=HEADER||capacity>244)throw new RangeError('Invalid message or ATT value capacity');
    const frames=[];
    for(let offset=0;offset<data.length;){const n=Math.min(capacity-HEADER,data.length-offset),frame=new Uint8Array(HEADER+n),view=new DataView(frame.buffer);
      frame.set([0xa7,1,channel,(offset===0?1:0)|(offset+n===data.length?2:0)]);view.setUint16(4,messageId,true);view.setUint16(6,offset,true);frame.set(data.subarray(offset,offset+n),HEADER);frames.push(frame);offset+=n;}
    return frames;
  }
  class FrameReassembler {
    constructor(channel,maxBytes=MAX_JSON){this.channel=channel;this.maxBytes=maxBytes;this.reset();}
    reset(){this.active=false;this.length=0;this.started=0;this.id=0;this.buffer=new Uint8Array(this.maxBytes);}
    push(input,now=Date.now()){
      const f=bytes(input);if(this.active&&now-this.started>=2000)this.reset();
      const invalid=()=>{this.reset();throw new Error('Invalid, out-of-order or expired BLE fragment');};
      if(f.length<=HEADER||f[0]!==0xa7||f[1]!==1||f[2]!==this.channel||(f[3]&~3))return invalid();
      const view=new DataView(f.buffer,f.byteOffset,f.byteLength),id=view.getUint16(4,true),offset=view.getUint16(6,true);
      if(f[3]&1){if(this.active||offset)return invalid();this.active=true;this.started=now;this.id=id;}
      if(!this.active||this.id!==id||offset!==this.length||f.length-HEADER>this.maxBytes-this.length)return invalid();
      this.buffer.set(f.subarray(HEADER),this.length);this.length+=f.length-HEADER;
      if(!(f[3]&2))return null;
      const result=this.buffer.slice(0,this.length);this.reset();decoder.decode(result);return result;
    }
  }
  function command(cmd,id,fields={}){
    if(!Number.isInteger(id)||id<1||id>0xffffffff)throw new RangeError('Invalid request ID');
    if(!['claim','heartbeat','release','start','stop','pause','set_resistance','set_heart_rate_address','scan_heart_rate','get_state'].includes(cmd))throw new Error('Unknown command');
    const out={v:1,id,cmd};
    if(cmd==='set_resistance'){if(!Number.isInteger(fields.level)||fields.level<0||fields.level>65535)throw new RangeError('阻力必須是 0–65535 的整數原始值');out.level=fields.level;}
    if(cmd==='set_heart_rate_address'){const address=String(fields.address??'').trim().toUpperCase();if(address&&!/^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(address))throw new Error('MAC 位址格式錯誤');out.address=address;}
    return out;
  }
  function sample(value,age,receivedAt,ttl,now=Date.now()) {return value!==null&&value!==undefined&&Number.isFinite(age)&&age>=0&&now-receivedAt+age<ttl?String(value):'—';}
  return Object.freeze({UUID,CHANNEL,MAX_JSON,MAX_COMMAND,HEADER,bytes,encodeJson,decodeJson,chunkPayload,FrameReassembler,command,sample});
});
