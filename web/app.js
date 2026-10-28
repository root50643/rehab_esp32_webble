(function(root){
  'use strict';
  const P=root.RehabProtocol,$=id=>document.getElementById(id);
  let device,commandChar,connected=false,controller=false,requestId=0,messageId=0,heartbeat,lastState,receivedAt=0,epoch=0;
  let writeChain=Promise.resolve();const pending=new Map(),subscriptions=[];
  const supported=!!(root.isSecureContext&&navigator.bluetooth);
  function log(message,data){$('log').textContent=(`${new Date().toLocaleTimeString()} ${message}${data?' '+JSON.stringify(data):''}\n`+$('log').textContent).slice(0,7000);}
  function say(message,bad=false){$('message').hidden=false;$('message').textContent=message;$('message').className='notice'+(bad?' warning':'');}
  function buttons(){
    $('connect').disabled=!supported||connected;$('disconnect').disabled=!connected;$('reconnect').hidden=connected||!device;
    $('claim').disabled=!connected||controller;$('release').disabled=!connected||!controller;
    for(const button of document.querySelectorAll('[data-command],#setResistance,#saveHeart,#scanHeart'))button.disabled=!connected||!controller;
    $('connection').textContent=connected?'已連線':'未連線';$('connection').className='badge'+(connected?' good':'');
  }
  function lease(value){if(!value)return;controller=connected&&value.you_control===true;buttons();$('leaseState').textContent=value.stopping?'正在完成失聯停止流程':controller?`你持有控制權 · 剩餘 ${Math.ceil(value.remaining_ms/1000)} 秒`:value.owner?`唯讀模式 · 控制者 ${value.owner}`:'唯讀模式 · 可取得控制權';}
  function render(){
    const s=lastState||{},h=s.heart||{},m=s.machine||{},r=s.rfid||{};const age=Date.now()-receivedAt;
    for(const [id,value,sourceAge,ttl] of [['bpm',h.bpm,h.age_ms,10000],['rpm',m.rpm,m.age28_ms,5000],['resistance',m.reported_level,m.age3f_ms,5000],['torque',m.torque_raw,m.age3f_ms,5000]]){
      const text=connected?P.sample(value,sourceAge,receivedAt,ttl):'—';$(id).textContent=text;$(id+'Age').textContent=text==='—'?'尚未收到或已過期':'資料有效';
    }
    $('machineState').textContent=connected?P.sample(m.state,m.state_age_ms,receivedAt,5000):'—';$('stateAge').textContent=connected&&age<5000?'最新快照（狀態依回報時效）':'連線中斷或過期';
    $('cardUid').textContent=connected&&age<5000&&r.card_present?(r.uid||'讀取中'):'—';
    $('extraData').textContent=JSON.stringify({machine_bpm:m.machine_bpm,minutes:m.minutes,seconds:m.seconds,distance_raw:m.distance_raw,calories_raw:m.calories_raw,watt28_raw:m.watt28_raw,watt3f_raw:m.watt3f_raw,target_level:m.target_level,sent_level:m.sent_level,level_ack_pending:m.level_ack_pending},null,2);
    if(!connected||age>=5000){controller=false;buttons();}
  }
  function scan(data){$('heartResults').replaceChildren();for(const item of data.results||[]){const li=document.createElement('li'),button=document.createElement('button');button.type='button';button.textContent=`${item.name||'未命名'} · ${item.address} · ${item.rssi} dBm`;button.onclick=()=>{$('heartMac').value=item.address;};li.append(button);$('heartResults').append(li);}if(data.truncated)say('顯示訊號最強的前 12 個候選。');}
  function notification(kind,assembler,event){
    try {const bytes=assembler.push(event.target.value);if(!bytes)return;const value=P.decodeJson(bytes);if(value.v!==1||value.type!==kind)throw new Error('Unsupported JSON schema');
      if(kind==='state'){lastState=value;receivedAt=Date.now();lease(value.lease);render();}
      else if(kind==='response'){lease(value.lease);const waiting=pending.get(value.id);if(waiting){clearTimeout(waiting.timer);pending.delete(value.id);value.ok?waiting.resolve(value):waiting.reject(new Error(value.error||'Command failed'));}log('命令回覆',value);}
      else {if(value.event==='heart_scan')scan(value.data||{});log('裝置事件',value);}
    }catch(error){say('資料接收錯誤：'+error.message,true);log('解碼失敗',{error:error.message});}
  }
  function send(cmd,fields={}){
    if(!connected||!commandChar)return Promise.reject(new Error('尚未連線'));
    let payload;try{payload=P.command(cmd,++requestId,fields);}catch(error){return Promise.reject(error);}
    const id=payload.id,session=epoch,target=commandChar;
    return new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{if(session===epoch)pending.delete(id);reject(new Error('命令回覆逾時；未自動重送'))},8000);pending.set(id,{resolve,reject,timer});
      writeChain=writeChain.catch(()=>{}).then(async()=>{
        if(!connected||session!==epoch)throw new Error('連線已中斷');
        // 20 bytes works at the BLE default MTU, across all supported browsers.
        for(const frame of P.chunkPayload(P.encodeJson(payload),20,++messageId,P.CHANNEL.command)){if(session!==epoch)throw new Error('連線已中斷');await target.writeValueWithResponse(frame);}
        log('送出命令',payload);
      }).catch(error=>{if(session!==epoch)return;const item=pending.get(id);if(item){clearTimeout(item.timer);pending.delete(id);item.reject(error);}});
    });
  }
  function disconnected(){
    ++epoch;connected=false;controller=false;commandChar=null;clearInterval(heartbeat);
    for(const [ch,listener] of subscriptions)ch.removeEventListener('characteristicvaluechanged',listener);subscriptions.length=0;
    for(const item of pending.values()){clearTimeout(item.timer);item.reject(new Error('連線已中斷'));}pending.clear();
    $('leaseState').textContent='已斷線 · 控制權釋放';buttons();render();say('連線已中斷，ESP32 將執行失聯停止。',true);
  }
  async function connect(reuse=false){
    if(!supported){say('請使用 HTTPS/localhost 與支援 Web Bluetooth 的 Chrome/Edge。',true);return;}
    try {
      $('connect').disabled=true;
      if(!reuse||!device){if(device)device.removeEventListener('gattserverdisconnected',disconnected);device=await navigator.bluetooth.requestDevice({filters:[{services:[P.UUID.service]}]});device.addEventListener('gattserverdisconnected',disconnected);}
      const server=await device.gatt.connect(),service=await server.getPrimaryService(P.UUID.service);
      const info=P.decodeJson(await(await service.getCharacteristic(P.UUID.info)).readValue());if(info.v!==1||info.protocol!==1)throw new Error('不支援的韌體協定版本');
      commandChar=await service.getCharacteristic(P.UUID.command);requestId=0;writeChain=Promise.resolve();
      for(const kind of ['response','event','state']){const ch=await service.getCharacteristic(P.UUID[kind]),assembler=new P.FrameReassembler(P.CHANNEL[kind]),listener=notification.bind(null,kind,assembler);ch.addEventListener('characteristicvaluechanged',listener);subscriptions.push([ch,listener]);await ch.startNotifications();}
      connected=true;controller=false;buttons();say(`已連線 ${device.name||'RehabWebBLE'}，目前為唯讀模式。`);log('韌體資訊',info);
      await send('get_state');heartbeat=setInterval(()=>{if(controller)send('heartbeat').catch(error=>{controller=false;buttons();say(error.message,true);});},10000);
    }catch(error){if(device?.gatt.connected)device.gatt.disconnect();else disconnected();say(error.name==='NotFoundError'?'已取消選擇裝置。':'連線失敗：'+error.message,true);}
  }
  function action(command,fields){return send(command,fields).then(()=>{say(command==='pause'?'暫停已確認：既有機台協定未定義實際暫停動作。':'命令已接受；實際機台值以 UART 回報為準。');}).catch(error=>say(error.message,true));}
  $('connect').onclick=()=>connect();$('reconnect').onclick=()=>connect(true);$('disconnect').onclick=()=>device?.gatt.disconnect();
  $('claim').onclick=()=>action('claim');$('release').onclick=()=>action('release');
  for(const b of document.querySelectorAll('[data-command]'))b.onclick=()=>action(b.dataset.command);
  $('setResistance').onclick=()=>action('set_resistance',{level:Number($('resistanceInput').value)});
  $('saveHeart').onclick=()=>action('set_heart_rate_address',{address:$('heartMac').value});$('scanHeart').onclick=()=>action('scan_heart_rate');
  $('secureNotice').hidden=supported;buttons();setInterval(render,500);
  root.RehabApp={send,connect,render};
})(window);
