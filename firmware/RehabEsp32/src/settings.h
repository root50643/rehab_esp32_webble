#pragma once
#include <Arduino.h>
#include <Preferences.h>
struct Settings {char heartAddress[18]={};uint8_t reserved[2]={};uint32_t revision=1;};
class SettingsStore {
  Preferences prefs_;Settings value_;bool healthy_=false;
public:
  void begin();Settings active()const{return value_;}
  bool saveHeartAddress(const String& address,String& error);
};
extern SettingsStore settingsStore;
