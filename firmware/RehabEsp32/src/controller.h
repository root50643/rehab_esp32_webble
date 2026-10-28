#pragma once
#include <Arduino.h>
#include <cJSON.h>
#include <freertos/semphr.h>
#include "protocol.h"
class Controller {
  SemaphoreHandle_t mutex_=nullptr;
  rehab::Parser uart_{0x55,0x90};rehab::MachineValues values_;
  rehab::Pulse pulse_;rehab::Fifo<rehab::PulseKind,8> actions_;rehab::Fifo<rehab::Frame,32> replies_;
  bool tracking_=false,hadUart_=false,levelPending_=false,levelQueued_=false;
  uint8_t levelChecksum_=0;int targetLevel_=-1,sentLevel_=-1,reportedState_=-1;
  uint32_t uartAt_=0,levelAt_=0,stateAt_=0,startAt_=0,timeouts_=0;
  String rxHex_,txHex_,error_;
  void run();void tick(uint32_t now);void machine(const rehab::Frame&,uint32_t);
  bool write(const rehab::Frame&);void flush(uint32_t now);void requestStopLocked();
  static void task(void* self){static_cast<Controller*>(self)->run();}
public:
  void begin();bool start();bool stop();bool pause();bool setResistance(int level);
  void failSafeStop();bool running();bool idle();void appendStatus(cJSON*,uint32_t now);
};
extern Controller controller;
