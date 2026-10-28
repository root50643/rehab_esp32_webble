#include "../firmware/RehabEsp32/src/settings_validation.h"
#include <cassert>
#include <initializer_list>
int main(){assert(rehab::validMac(""));assert(rehab::validMac("AA:BB:CC:DD:EE:FF"));assert(rehab::validMac("aa:bb:cc:dd:ee:ff"));for(const char* value:{"aabbccddeeff","AA:BB:CC:DD:EE:FG","AA:BB:CC:DD:EE"," AA:BB:CC:DD:EE:FF"})assert(!rehab::validMac(value));assert(!rehab::validMac(nullptr));}
