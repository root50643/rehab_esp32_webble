#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
namespace rehab {
inline bool validMac(const char* s) {
  if (!s) return false;
  if (!*s) return true; // Explicitly disable the BLE target.
  if (strlen(s) != 17) return false;
  for (size_t i=0;i<17;++i) if (i%3 == 2 ? s[i] != ':' : !isxdigit((unsigned char)s[i])) return false;
  return true;
}
}
