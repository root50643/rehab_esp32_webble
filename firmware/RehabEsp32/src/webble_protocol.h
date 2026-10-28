#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace webble {
constexpr uint8_t kMagic=0xa7, kVersion=1;
constexpr size_t kHeader=8, kMaxJson=4096, kMaxCommand=1024;
constexpr uint32_t kAssemblyMs=2000;
enum class Channel : uint8_t { State=1, Event=2, Command=3, Response=4, Info=5 };
inline bool elapsed(uint32_t now,uint32_t then,uint32_t ms) {return uint32_t(now-then)>=ms;}
inline uint16_t read16(const uint8_t* b) {return uint16_t(b[0])|(uint16_t(b[1])<<8);}
inline void write16(uint8_t* b,uint16_t v) {b[0]=uint8_t(v);b[1]=uint8_t(v>>8);}
// Reject malformed/overlong UTF-8, embedded NUL, surrogate and out-of-range scalars.
inline bool validUtf8(const uint8_t* p,size_t n) {
  if(!p && n) return false;
  for(size_t i=0;i<n;) {
    uint8_t c=p[i++]; if(!c) return false; if(c<0x80) continue;
    uint32_t value=0,min=0; size_t more=0;
    if(c>=0xc2 && c<=0xdf){value=c&31;more=1;min=0x80;}
    else if(c>=0xe0 && c<=0xef){value=c&15;more=2;min=0x800;}
    else if(c>=0xf0 && c<=0xf4){value=c&7;more=3;min=0x10000;}
    else return false;
    if(n-i<more) return false;
    while(more--){uint8_t b=p[i++];if((b&0xc0)!=0x80)return false;value=(value<<6)|(b&63);}
    if(value<min || value>0x10ffff || (value>=0xd800 && value<=0xdfff))return false;
  }
  return true;
}
enum class Assembly { Partial, Complete, Invalid, Timeout };
template<size_t Capacity=kMaxCommand> class Assembler {
  uint8_t bytes_[Capacity+1]{};
  size_t length_=0; uint16_t id_=0; uint32_t started_=0;
  uint8_t type_=0; bool active_=false;
 public:
  void reset(){length_=0;active_=false;}
  const uint8_t* data()const{return bytes_;}
  size_t size()const{return length_;}
  bool active()const{return active_;}
  bool expire(uint32_t now){if(active_&&elapsed(now,started_,kAssemblyMs)){reset();return true;}return false;}
  Assembly push(const uint8_t* frame,size_t n,uint32_t now,Channel expected) {
    bool timedOut=expire(now);
    if(!frame||n<=kHeader||frame[0]!=kMagic||frame[1]!=kVersion||frame[2]!=uint8_t(expected)||(frame[3]&~3)) {reset();return Assembly::Invalid;}
    const uint8_t flags=frame[3];const uint16_t id=read16(frame+4),offset=read16(frame+6);
    if(flags&1){if(active_||offset){reset();return Assembly::Invalid;}id_=id;type_=frame[2];started_=now;length_=0;active_=true;}
    if(!active_) return timedOut?Assembly::Timeout:Assembly::Invalid;
    if(id!=id_||type_!=frame[2]||offset!=length_||n-kHeader>Capacity-length_) {reset();return Assembly::Invalid;}
    memcpy(bytes_+length_,frame+kHeader,n-kHeader);length_+=n-kHeader;bytes_[length_]=0;
    if(flags&2){active_=false;if(!validUtf8(bytes_,length_)){reset();return Assembly::Invalid;}return Assembly::Complete;}
    return Assembly::Partial;
  }
};
inline size_t fragment(const uint8_t* json,size_t total,Channel channel,uint16_t id,size_t offset,
                       uint8_t* out,size_t capacity) {
  if(!json||!out||!total||total>kMaxJson||offset>=total||capacity<=kHeader)return 0;
  size_t n=total-offset;if(n>capacity-kHeader)n=capacity-kHeader;
  out[0]=kMagic;out[1]=kVersion;out[2]=uint8_t(channel);
  out[3]=uint8_t((offset==0?1:0)|(offset+n==total?2:0));
  write16(out+4,id);write16(out+6,uint16_t(offset));memcpy(out+kHeader,json+offset,n);return n+kHeader;
}
}
