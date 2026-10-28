#pragma once
#include <stdint.h>
namespace webble {
class ControlLease {
  uint32_t owner_=0, renewed_=0;
 public:
  static constexpr uint32_t lifetimeMs=30000;
  uint32_t owner()const{return owner_;}
  bool owns(uint32_t peer)const{return peer && peer==owner_;}
  bool claim(uint32_t peer,uint32_t now){if(!peer||(owner_&&owner_!=peer))return false;owner_=peer;renewed_=now;return true;}
  bool renew(uint32_t peer,uint32_t now){if(!owns(peer))return false;renewed_=now;return true;}
  bool release(uint32_t peer){if(!owns(peer))return false;owner_=0;return true;}
  bool expire(uint32_t now){if(owner_&&uint32_t(now-renewed_)>=lifetimeMs){owner_=0;return true;}return false;}
  uint32_t remaining(uint32_t now)const{uint32_t age=uint32_t(now-renewed_);return !owner_||age>=lifetimeMs?0:lifetimeMs-age;}
};
}
