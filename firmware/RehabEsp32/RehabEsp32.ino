#include "src/board_config.h"
#include "src/settings.h"
#include "src/ble_service.h"
#include "src/ccid_service.h"
#include "src/controller.h"
#include "src/gateway_service.h"
void setup(){
  Serial.begin(115200);
  settingsStore.begin();controller.begin();ccidService.begin();
  gateway::begin();
  bleService.begin(settingsStore.active().heartAddress);
}
void loop(){gateway::tick();delay(2);}
