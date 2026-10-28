#include "gateway_service.h"
#include "board_config.h"
#include "controller.h"
#include "ble_service.h"
#include "ccid_service.h"
#include "settings.h"
#include "json_helpers.h"
#include "webble_protocol.h"
#include "control_lease.h"
#include <NimBLEDevice.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <cmath>

namespace gateway {
namespace {
constexpr const char* kService="7e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kState="7e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kEvent="7e400003-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kCommand="7e400004-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kResponse="7e400005-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kInfo="7e400006-b5a3-f393-e0a9-e50e24dcca9e";
constexpr uint16_t kNoConnection=0xffff;
constexpr size_t kPeers=4;
struct Outgoing {webble::Channel channel=webble::Channel::State;String json;};
struct Peer {
  uint16_t connection=kNoConnection;uint32_t key=0,lastRequest=0;
  uint8_t subscriptions=0,failures=0;
  webble::Assembler<> assembler;
  rehab::Fifo<Outgoing,8> replies,events;
  Outgoing active;String latestState,lastResponse;
  size_t offset=0;uint16_t message=0;
  uint32_t lastState=0,lastPacket=0,started=0,dropped=0;
};
struct Incoming {uint16_t connection;uint32_t key;char json[webble::kMaxCommand+1];};
SemaphoreHandle_t mutex=nullptr;QueueHandle_t incoming=nullptr;
NimBLEServer* server=nullptr;
NimBLECharacteristic *stateChar=nullptr,*eventChar=nullptr,*responseChar=nullptr;
Peer peers[kPeers];webble::ControlLease lease;
uint32_t nextKey=0,sequence=0,eventSequence=0,lastSample=0;
uint32_t lastAdvertiseAttempt=0;
bool initialized=false,stopRequired=false,advertiseRequired=false,scanReport=false;
uint32_t lastHeartDisconnects=0,lastReaderErrors=0;
bool lastPresent=false,lastHeartValid=false;
class Lock {public:Lock(){xSemaphoreTake(mutex,portMAX_DELAY);}~Lock(){xSemaphoreGive(mutex);}};
Peer* find(uint16_t connection){for(auto& p:peers)if(p.connection==connection)return &p;return nullptr;}
String encoded(cJSON* json){char* s=cJSON_PrintUnformatted(json);String text=s?s:"";if(s)cJSON_free(s);cJSON_Delete(json);return text;}
void appendLease(cJSON* root,const Peer* peer,uint32_t now){
  auto* o=cJSON_AddObjectToObject(root,"lease");
  if(lease.owner())jn(o,"owner",lease.owner());else cJSON_AddNullToObject(o,"owner");
  jn(o,"remaining_ms",lease.remaining(now));jb(o,"you_control",peer&&lease.owns(peer->key));jb(o,"stopping",stopRequired);
}
String infoJson(const Peer* peer=nullptr){
  auto* o=cJSON_CreateObject();jn(o,"v",1);js(o,"type","info");js(o,"firmware",board::firmwareVersion);
  jn(o,"protocol",1);jn(o,"max_web_peers",kPeers);jn(o,"total_connections",CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
  jn(o,"max_json_bytes",webble::kMaxJson);jn(o,"max_command_bytes",webble::kMaxCommand);
  jn(o,"lease_ms",webble::ControlLease::lifetimeMs);jn(o,"heartbeat_ms",10000);jn(o,"max_state_hz",10);
  jn(o,"peer_key",peer?peer->key:0);jb(o,"torque_writable",false);return encoded(o);
}
void disconnectSlow(Peer& peer){server->disconnect(peer.connection);}
void queue(Peer& peer,webble::Channel channel,const String& json){
  if(json.isEmpty()||json.length()>webble::kMaxJson)return;
  Outgoing out{channel,json};
  if(channel==webble::Channel::Response){if(!peer.replies.push(out))disconnectSlow(peer);}
  else if(!peer.events.push(out))++peer.dropped;
}
void event(const char* name,cJSON* detail=nullptr){
  Serial.printf("WebBLE event: %s\n",name);
  auto* o=cJSON_CreateObject();jn(o,"v",1);js(o,"type","event");jn(o,"seq",++eventSequence);jn(o,"uptime_ms",millis());js(o,"event",name);
  if(detail)cJSON_AddItemToObject(o,"data",detail);
  String text=encoded(o);for(auto& p:peers)if(p.connection!=kNoConnection&&(p.subscriptions&2))queue(p,webble::Channel::Event,text);
}
String stateJson(Peer& peer,uint32_t now){
  auto* o=cJSON_CreateObject();jn(o,"v",1);js(o,"type","state");jn(o,"seq",sequence);jn(o,"uptime_ms",now);
  auto* heart=cJSON_AddObjectToObject(o,"heart");bleService.appendStatus(heart,now);
  cJSON_DeleteItemFromObjectCaseSensitive(heart,"last_notification_hex");
  auto h=bleService.snapshot();now=millis();if(!h.valid||!h.ready||webble::elapsed(now,h.receivedMs,10000))cJSON_ReplaceItemInObjectCaseSensitive(heart,"bpm",cJSON_CreateNull());
  auto* machine=cJSON_AddObjectToObject(o,"machine");controller.appendStatus(machine,now);
  cJSON_DeleteItemFromObjectCaseSensitive(machine,"rx_hex");cJSON_DeleteItemFromObjectCaseSensitive(machine,"tx_hex");
  auto* rfid=cJSON_AddObjectToObject(o,"rfid");ccidService.appendStatus(rfid,now);
  for(const char* key:{"atr","last_tx","last_rx"})cJSON_DeleteItemFromObjectCaseSensitive(rfid,key);
  appendLease(o,&peer,now);jn(o,"peer_key",peer.key);jn(o,"dropped_events",peer.dropped);
  jn(o,"free_heap",ESP.getFreeHeap());jn(o,"min_free_heap",ESP.getMinFreeHeap());return encoded(o);
}
void response(Peer& peer,uint32_t id,const char* cmd,bool ok,const char* error=nullptr,cJSON* data=nullptr){
  auto* o=cJSON_CreateObject();jn(o,"v",1);js(o,"type","response");jn(o,"id",id);js(o,"cmd",cmd?cmd:"");jb(o,"ok",ok);
  if(error)js(o,"error",error);
  if(data)cJSON_AddItemToObject(o,"data",data);
  jn(o,"peer_key",peer.key);appendLease(o,&peer,millis());
  String text=encoded(o);if(id){peer.lastRequest=id;peer.lastResponse=text;}queue(peer,webble::Channel::Response,text);
}
bool wholeNumber(cJSON* field,double lo,double hi){return cJSON_IsNumber(field)&&std::isfinite(field->valuedouble)&&field->valuedouble>=lo&&field->valuedouble<=hi&&std::floor(field->valuedouble)==field->valuedouble;}
bool schema(cJSON* root){
  if(!cJSON_IsObject(root))return false;
  const char* allowed[]={"v","id","cmd","level","address"};bool seen[5]{};
  for(auto* p=root->child;p;p=p->next){int at=-1;for(int i=0;i<5;++i)if(p->string&&!strcmp(p->string,allowed[i]))at=i;if(at<0||seen[at])return false;seen[at]=true;}
  return wholeNumber(cJSON_GetObjectItemCaseSensitive(root,"v"),1,1)&&wholeNumber(cJSON_GetObjectItemCaseSensitive(root,"id"),1,4294967295.0)&&cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root,"cmd"));
}
void process(Peer& peer,const char* text,uint32_t now){
  // Bound cJSON recursion before parsing untrusted GATT input.
  int depth=0;bool quoted=false,escaped=false,valid=true;
  for(const char* p=text;*p;++p){if(quoted){if(escaped)escaped=false;else if(*p=='\\')escaped=true;else if(*p=='"')quoted=false;}
    else if(*p=='"')quoted=true;else if(*p=='{'||*p=='['){if(++depth>8)valid=false;}else if(*p=='}'||*p==']'){if(--depth<0)valid=false;}}
  if(!valid||quoted||depth||strstr(text,"\\u0000")){response(peer,0,"",false,"invalid_json_or_schema");return;}
  const char* end=nullptr;auto* req=cJSON_ParseWithOpts(text,&end,true);
  if(!req||!schema(req)){if(req)cJSON_Delete(req);response(peer,0,"",false,"invalid_json_or_schema");return;}
  const uint32_t id=uint32_t(cJSON_GetObjectItemCaseSensitive(req,"id")->valuedouble);
  const char* cmd=cJSON_GetObjectItemCaseSensitive(req,"cmd")->valuestring;
  if(id==peer.lastRequest){queue(peer,webble::Channel::Response,peer.lastResponse);cJSON_Delete(req);return;}
  if(id<peer.lastRequest){response(peer,0,cmd,false,"stale_request");cJSON_Delete(req);return;}
  bool ok=true;const char* error=nullptr;
  if(!strcmp(cmd,"get_state")){peer.latestState=stateJson(peer,now);}
  else if(!strcmp(cmd,"claim")){
    if(stopRequired){ok=false;error="stopping";}
    else if(!lease.claim(peer.key,now)){ok=false;error="controller_busy";}
  }else if(!lease.owns(peer.key)){ok=false;error="not_controller";}
  else if(!strcmp(cmd,"heartbeat")){lease.renew(peer.key,now);}
  else if(!strcmp(cmd,"release")){lease.release(peer.key);controller.failSafeStop();stopRequired=!controller.idle();}
  else if(!strcmp(cmd,"start")){ok=controller.start();if(!ok)error="machine_busy";}
  else if(!strcmp(cmd,"stop")){ok=controller.stop();if(!ok)error="machine_busy";}
  else if(!strcmp(cmd,"pause")){ok=controller.pause();if(!ok)error="machine_busy";}
  else if(!strcmp(cmd,"set_resistance")){
    auto* level=cJSON_GetObjectItemCaseSensitive(req,"level");
    if(!wholeNumber(level,0,65535)){ok=false;error="invalid_level";}
    else {ok=controller.setResistance(int(level->valuedouble));if(!ok)error="machine_busy";}
  }else if(!strcmp(cmd,"set_heart_rate_address")){
    auto* address=cJSON_GetObjectItemCaseSensitive(req,"address");String problem;
    if(!controller.idle()){ok=false;error="machine_busy";}
    else if(!cJSON_IsString(address)){ok=false;error="invalid_address";}
    else if(!settingsStore.saveHeartAddress(address->valuestring,problem)){ok=false;error="invalid_address_or_storage_error";}
    else bleService.configure(settingsStore.active().heartAddress);
  }else if(!strcmp(cmd,"scan_heart_rate")){
    if(bleService.scanBusy()){ok=false;error="scan_busy";}
    else {ok=bleService.requestScan();scanReport=ok;if(!ok)error="scan_busy";}
  }else {ok=false;error="unknown_command";}
  response(peer,id,cmd,ok,error);cJSON_Delete(req);
}
class ServerCallbacks final:public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s,NimBLEConnInfo& info)override{
    Lock guard;Peer* free=nullptr;for(auto& p:peers)if(p.connection==kNoConnection){free=&p;break;}
    if(!free){s->disconnect(info.getConnHandle());return;}
    *free=Peer{};free->connection=info.getConnHandle();free->key=++nextKey;if(!free->key)free->key=++nextKey;advertiseRequired=true;
  }
  void onDisconnect(NimBLEServer*,NimBLEConnInfo& info,int)override{
    Lock guard;if(auto* p=find(info.getConnHandle())){if(lease.release(p->key))stopRequired=true;*p=Peer{};}advertiseRequired=true;
  }
};
class CharacteristicCallbacks final:public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic,NimBLEConnInfo& info)override{
    Lock guard;auto* p=find(info.getConnHandle());if(!p)return;
    const auto& value=characteristic->getValue();
    auto result=p->assembler.push(value.data(),value.size(),millis(),webble::Channel::Command);
    if(result==webble::Assembly::Complete){Incoming in{};in.connection=p->connection;in.key=p->key;memcpy(in.json,p->assembler.data(),p->assembler.size()+1);if(xQueueSend(incoming,&in,0)!=pdTRUE)disconnectSlow(*p);}
    else if(result==webble::Assembly::Invalid||result==webble::Assembly::Timeout)disconnectSlow(*p);
  }
  void onSubscribe(NimBLECharacteristic* characteristic,NimBLEConnInfo& info,uint16_t value)override{
    Lock guard;if(auto* p=find(info.getConnHandle())){uint8_t mask=characteristic==stateChar?1:characteristic==eventChar?2:4;if(value&1)p->subscriptions|=mask;else p->subscriptions&=~mask;}
  }
  void onRead(NimBLECharacteristic* characteristic,NimBLEConnInfo& info)override{Lock guard;characteristic->setValue(infoJson(find(info.getConnHandle())).c_str());}
};
ServerCallbacks serverCallbacks;CharacteristicCallbacks characteristicCallbacks;
void sendPeer(Peer& p,uint32_t now){
  if(p.connection==kNoConnection||!webble::elapsed(now,p.lastPacket,4))return;
  if(p.active.json.isEmpty()){
    if(!p.replies.pop(p.active)&&!p.events.pop(p.active)){
      if(p.latestState.isEmpty()||!(p.subscriptions&1))return;
      p.active={webble::Channel::State,p.latestState};p.latestState="";
    }
    p.offset=0;++p.message;p.started=now;
  }
  const auto kind=p.active.channel;
  const uint8_t mask=kind==webble::Channel::State?1:kind==webble::Channel::Event?2:4;
  if(!(p.subscriptions&mask)){p.active=Outgoing{};return;}
  if(webble::elapsed(now,p.started,1800)){disconnectSlow(p);return;}
  const uint16_t mtu=server->getPeerMTU(p.connection);uint8_t frame[244];
  size_t capacity=mtu>3?size_t(mtu-3):20;if(capacity>sizeof(frame))capacity=sizeof(frame);
  auto* characteristic=kind==webble::Channel::State?stateChar:kind==webble::Channel::Event?eventChar:responseChar;
  p.lastPacket=now;
  // Two small packets per turn keep a 4096-byte message within the assembly
  // deadline at the default MTU, without waiting for the radio or a peer.
  for(int budget=0;budget<(capacity<100?2:1);++budget){
    size_t n=webble::fragment(reinterpret_cast<const uint8_t*>(p.active.json.c_str()),p.active.json.length(),kind,p.message,p.offset,frame,capacity);
    if(n&&characteristic->notify(frame,n,p.connection)){
      p.offset+=n-webble::kHeader;p.failures=0;if(p.offset==p.active.json.length()){p.active=Outgoing{};break;}
    }else {if(++p.failures>=100)disconnectSlow(p);break;}
  }
}
}
void begin(){
  mutex=xSemaphoreCreateMutex();incoming=xQueueCreate(16,sizeof(Incoming));if(!mutex||!incoming){Serial.println("BLE gateway allocation failed");return;}
  if(!NimBLEDevice::init("RehabWebBLE")){Serial.println("BLE host init failed");return;}
  NimBLEDevice::setMTU(247);server=NimBLEDevice::createServer();server->setCallbacks(&serverCallbacks);server->advertiseOnDisconnect(false);
  auto* service=server->createService(kService);
  stateChar=service->createCharacteristic(kState,NIMBLE_PROPERTY::NOTIFY,244);
  eventChar=service->createCharacteristic(kEvent,NIMBLE_PROPERTY::NOTIFY,244);
  responseChar=service->createCharacteristic(kResponse,NIMBLE_PROPERTY::NOTIFY,244);
  auto* command=service->createCharacteristic(kCommand,NIMBLE_PROPERTY::WRITE,244);
  auto* info=service->createCharacteristic(kInfo,NIMBLE_PROPERTY::READ,512);
  for(auto* c:{stateChar,eventChar,responseChar,command,info})c->setCallbacks(&characteristicCallbacks);
  info->setValue(infoJson().c_str());if(!server->start()){Serial.println("GATT server start failed");return;}
  auto* advertising=NimBLEDevice::getAdvertising();advertising->addServiceUUID(kService);advertising->enableScanResponse(true);advertising->setName("RehabWebBLE");
  initialized=true;advertiseRequired=true;lastAdvertiseAttempt=millis()-1000;Serial.println("WebBLE v1 ready: RehabWebBLE; 4 browser peers + 1 heart rate link");
}
void tick(){
  if(!initialized)return;
  Lock guard;uint32_t now=millis();
  if(lease.expire(now))stopRequired=true;
  if(stopRequired){controller.failSafeStop();if(controller.idle())stopRequired=false;}
  if(advertiseRequired&&webble::elapsed(now,lastAdvertiseAttempt,1000)){lastAdvertiseAttempt=now;size_t count=0;for(auto& p:peers)if(p.connection!=kNoConnection)++count;
    if(count<kPeers)advertiseRequired=!NimBLEDevice::getAdvertising()->start();else {NimBLEDevice::getAdvertising()->stop();advertiseRequired=false;}}
  Incoming in{};for(int i=0;i<4&&xQueueReceive(incoming,&in,0)==pdTRUE;++i){auto* p=find(in.connection);if(p&&p->key==in.key)process(*p,in.json,now);}
  if(webble::elapsed(now,lastSample,100)){
    lastSample=now;++sequence;
    for(auto& p:peers)if(p.connection!=kNoConnection){
      if(p.assembler.expire(now))disconnectSlow(p);
      uint32_t interval=server->getPeerMTU(p.connection)<100?1000:100;
      if((p.subscriptions&1)&&webble::elapsed(now,p.lastState,interval)){p.lastState=now;p.latestState=stateJson(p,now);}
    }
    auto heart=bleService.snapshot();if(heart.disconnects!=lastHeartDisconnects){lastHeartDisconnects=heart.disconnects;event("heart_disconnected");}
    if(heart.valid!=lastHeartValid){lastHeartValid=heart.valid;event(heart.valid?"heart_valid":"heart_stale");}
    auto reader=ccidService.snapshot();if(reader.cardPresent!=lastPresent){lastPresent=reader.cardPresent;event(reader.cardPresent?"card_present":"card_removed");}
    if(reader.errors!=lastReaderErrors){lastReaderErrors=reader.errors;auto* detail=cJSON_CreateObject();js(detail,"message",reader.lastError);event("reader_error",detail);}
    String uid;while(ccidService.takeCard(uid)){auto* detail=cJSON_CreateObject();js(detail,"uid",uid);event("card",detail);}
    if(scanReport&&!bleService.scanBusy()){
      scanReport=false;auto* scan=cJSON_CreateObject();bleService.appendScan(scan);auto* list=cJSON_GetObjectItemCaseSensitive(scan,"results");
      bool truncated=cJSON_GetArraySize(list)>12;while(cJSON_GetArraySize(list)>12)cJSON_DeleteItemFromArray(list,12);jb(scan,"truncated",truncated);event("heart_scan",scan);
    }
  }
  for(auto& p:peers)sendPeer(p,now);
}
}
