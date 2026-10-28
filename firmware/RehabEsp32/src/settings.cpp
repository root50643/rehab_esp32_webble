#include "settings.h"
#include "settings_validation.h"
#include <esp_crc.h>
SettingsStore settingsStore;
namespace {
struct Record {uint32_t magic=0x52424c45,version=1;Settings value;uint32_t crc=0;};
uint32_t crc(const Settings& s){return esp_crc32_le(0,reinterpret_cast<const uint8_t*>(&s),sizeof(s));}
}
void SettingsStore::begin(){
  healthy_=prefs_.begin("rehab_webble",false);Record record;
  if(healthy_&&prefs_.getBytesLength("heart")==sizeof(record)&&prefs_.getBytes("heart",&record,sizeof(record))==sizeof(record)&&record.magic==0x52424c45&&record.version==1&&record.crc==crc(record.value)&&memchr(record.value.heartAddress,0,sizeof(record.value.heartAddress))&&rehab::validMac(record.value.heartAddress))value_=record.value;
}
bool SettingsStore::saveHeartAddress(const String& input,String& error){
  String address=input;address.trim();address.toUpperCase();if(!rehab::validMac(address.c_str())){error="Invalid MAC address";return false;}
  if(!healthy_||value_.revision==UINT32_MAX){error="NVS unavailable";return false;}
  Settings next;strlcpy(next.heartAddress,address.c_str(),sizeof(next.heartAddress));next.revision=value_.revision+1;
  Record record;record.value=next;record.crc=crc(next);if(prefs_.putBytes("heart",&record,sizeof(record))!=sizeof(record)){error="NVS write failed";return false;}value_=next;return true;
}
