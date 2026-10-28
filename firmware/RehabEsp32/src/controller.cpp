#include "controller.h"
#include "board_config.h"
#include "json_helpers.h"
#include <driver/gpio.h>
#include <freertos/task.h>
Controller controller;
static HardwareSerial machineSerial(1);
void Controller::begin(){
  mutex_=xSemaphoreCreateMutex();if(!mutex_)return;
  gpio_set_level(static_cast<gpio_num_t>(board::startPin),0);gpio_set_level(static_cast<gpio_num_t>(board::stopPin),0);
  pinMode(board::startPin,OUTPUT);pinMode(board::stopPin,OUTPUT);digitalWrite(board::startPin,LOW);digitalWrite(board::stopPin,LOW);
  machineSerial.setRxBufferSize(2048);machineSerial.begin(board::machineBaud,SERIAL_8N1,board::machineRx,board::machineTx);
  if(xTaskCreate(task,"machine",6144,this,3,nullptr)!=pdPASS)error_="Cannot start machine worker";
}
bool Controller::start(){
  if(!mutex_)return false;xSemaphoreTake(mutex_,portMAX_DELAY);
  bool ok=!tracking_&&pulse_.kind==rehab::PulseKind::None&&actions_.size()==0;
  if(ok)ok=actions_.push(rehab::PulseKind::Start);xSemaphoreGive(mutex_);return ok;
}
void Controller::requestStopLocked(){
  actions_.clear();levelQueued_=false;
  // A lease loss must not restart or extend a Stop pulse on every gateway tick.
  if(pulse_.kind==rehab::PulseKind::Stop)return;
  digitalWrite(board::startPin,LOW);pulse_.start(rehab::PulseKind::Stop,millis(),0);
}
bool Controller::stop(){if(!mutex_)return false;xSemaphoreTake(mutex_,portMAX_DELAY);requestStopLocked();xSemaphoreGive(mutex_);return true;}
void Controller::failSafeStop(){
  if(!mutex_)return;xSemaphoreTake(mutex_,portMAX_DELAY);
  if(tracking_||pulse_.kind!=rehab::PulseKind::None||actions_.size())requestStopLocked();
  else levelQueued_=false;
  xSemaphoreGive(mutex_);
}
// The original machine protocol defines Pause as acknowledgement only.
bool Controller::pause(){return mutex_!=nullptr;}
bool Controller::setResistance(int value){
  if(!mutex_||value<0||value>65535)return false;xSemaphoreTake(mutex_,portMAX_DELAY);
  bool ok=pulse_.kind!=rehab::PulseKind::Stop;
  if(ok){targetLevel_=value;levelQueued_=value>0;}xSemaphoreGive(mutex_);return ok;
}
bool Controller::running(){if(!mutex_)return false;xSemaphoreTake(mutex_,portMAX_DELAY);bool v=tracking_;xSemaphoreGive(mutex_);return v;}
bool Controller::idle(){if(!mutex_)return false;xSemaphoreTake(mutex_,portMAX_DELAY);bool v=!tracking_&&pulse_.kind==rehab::PulseKind::None&&!actions_.size()&&!levelPending_&&!levelQueued_;xSemaphoreGive(mutex_);return v;}
bool Controller::write(const rehab::Frame& f){
  uint8_t bytes[260];size_t n=rehab::encode(f,0x55,0x90,bytes);
  if(machineSerial.availableForWrite()<int(n))return false;
  if(machineSerial.write(bytes,n)!=n){error_="UART write failed";return false;}txHex_=hexBytes(bytes,n);return true;
}
void Controller::flush(uint32_t now){
  while(const auto* reply=replies_.peek()){if(!write(*reply))return;rehab::Frame ignored;replies_.pop(ignored);}
  if(!levelPending_&&levelQueued_){
    uint8_t data[]={uint8_t(targetLevel_>>8),uint8_t(targetLevel_),0,0};auto f=rehab::makeFrame(0x44,data,4);
    if(write(f)){sentLevel_=targetLevel_;levelQueued_=false;levelPending_=true;levelAt_=now;levelChecksum_=rehab::checksum(f);}
  }
}
void Controller::machine(const rehab::Frame& f,uint32_t now){
  bool valid=(f.command==0x27&&f.length==1&&f.data[0]<=3)||(f.command==0x28&&f.length==12)||(f.command==0x30&&f.length==0)||(f.command==0x3f&&f.length==6)||(f.command==0x44&&f.length==1);
  if(!valid){error_="Invalid UART command or length";return;}
  hadUart_=true;uartAt_=now;uint8_t bytes[260];size_t n=rehab::encode(f,0x55,0x90,bytes);rxHex_=hexBytes(bytes,n);
  rehab::Frame reply;bool ack=true;
  if(f.command==0x27){reportedState_=f.data[0];stateAt_=now;reply=f;if(reportedState_==1&&!tracking_){tracking_=true;startAt_=now;}else if(reportedState_==0||reportedState_==3)tracking_=false;}
  else if(f.command==0x30){reportedState_=1;stateAt_=now;if(!tracking_)startAt_=now;tracking_=true;uint8_t ok=1;reply=rehab::makeFrame(0x30,&ok,1);}
  else if(f.command==0x44){ack=false;if(levelPending_&&f.data[0]==levelChecksum_)levelPending_=false;else error_="Unmatched resistance ACK";}
  else {values_.accept(f,now);uint8_t check=rehab::checksum(f);reply=rehab::makeFrame(f.command,&check,1);}
  if(ack&&!replies_.push(reply))error_="UART reply queue full";
}
void Controller::tick(uint32_t now){
  size_t budget=512;while(budget--&&machineSerial.available()){uart_.feed(uint8_t(machineSerial.read()),now);rehab::Frame f;while(uart_.next(f))machine(f,now);}
  uart_.expire(now);rehab::Frame f;while(uart_.next(f))machine(f,now);
  if(levelPending_&&rehab::due(now,levelAt_,3000)){levelPending_=false;++timeouts_;error_="Resistance UART ACK timeout";}
  if(tracking_&&rehab::due(now,startAt_,31000)&&(!hadUart_||rehab::due(now,uartAt_,31000))){error_="Machine UART timeout; stopping";++timeouts_;requestStopLocked();}
  flush(now);
  if(pulse_.kind==rehab::PulseKind::None&&actions_.size()){rehab::PulseKind next;actions_.pop(next);pulse_.start(next,now,0);}
  if(pulse_.kind!=rehab::PulseKind::None){
    bool complete=pulse_.tick(now);
    digitalWrite(board::startPin,pulse_.kind==rehab::PulseKind::Start&&pulse_.high?HIGH:LOW);
    digitalWrite(board::stopPin,pulse_.kind==rehab::PulseKind::Stop&&pulse_.high?HIGH:LOW);
    if(complete){tracking_=pulse_.kind==rehab::PulseKind::Start;if(tracking_)startAt_=now;pulse_.kind=rehab::PulseKind::None;}
  }
}
void Controller::run(){for(;;){xSemaphoreTake(mutex_,portMAX_DELAY);tick(millis());xSemaphoreGive(mutex_);vTaskDelay(pdMS_TO_TICKS(5));}}
void Controller::appendStatus(cJSON* o,uint32_t now){
  if(!mutex_){js(o,"last_error","Controller unavailable");return;}xSemaphoreTake(mutex_,portMAX_DELAY);now=millis();
  const char* names[]={"Idle","Start","Pause","End"};if(reportedState_>=0)js(o,"state",names[reportedState_]);else cJSON_AddNullToObject(o,"state");
  jnullable(o,"state_age_ms",now-stateAt_,reportedState_>=0);jb(o,"tracking",tracking_);jb(o,"uart_online",hadUart_&&!rehab::due(now,uartAt_,5000));jnullable(o,"age_ms",now-uartAt_,hadUart_);
  bool fresh28=values_.has28&&!rehab::due(now,values_.at28,5000),fresh3f=values_.has3f&&!rehab::due(now,values_.at3f,5000);
  jnullable(o,"rpm",values_.rpm,fresh28);jnullable(o,"machine_bpm",values_.bpm,fresh28);jnullable(o,"minutes",values_.minutes,fresh28);jnullable(o,"seconds",values_.seconds,fresh28);
  jnullable(o,"distance_raw",values_.distance,fresh28);jnullable(o,"calories_raw",values_.calories,fresh28);jnullable(o,"watt28_raw",values_.watt28,fresh28);
  jnullable(o,"reported_level",values_.level,fresh3f);jnullable(o,"watt3f_raw",values_.watt3f,fresh3f);jnullable(o,"torque_raw",values_.torque,fresh3f);
  jnullable(o,"age28_ms",now-values_.at28,values_.has28);jnullable(o,"age3f_ms",now-values_.at3f,values_.has3f);
  jnullable(o,"target_level",targetLevel_,targetLevel_>=0);jnullable(o,"sent_level",sentLevel_,sentLevel_>=0);jb(o,"level_ack_pending",levelPending_);jb(o,"level_queued",levelQueued_);
  js(o,"pulse",pulse_.kind==rehab::PulseKind::None?"idle":pulse_.kind==rehab::PulseKind::Start?"start":"stop");jn(o,"queued_controls",actions_.size());js(o,"last_error",error_);
  jn(o,"checksum_errors",uart_.checksumErrors);jn(o,"framing_errors",uart_.framingErrors);jn(o,"timeouts",timeouts_+uart_.timeouts);xSemaphoreGive(mutex_);
}
