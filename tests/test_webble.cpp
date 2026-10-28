#include "../firmware/RehabEsp32/src/webble_protocol.h"
#include "../firmware/RehabEsp32/src/control_lease.h"
#include <cassert>
#include <vector>
#include <string>
#include <iostream>
using namespace webble;
int main(){
  const std::string text="{\"v\":1,\"id\":17,\"cmd\":\"claim\"}";
  Assembler<> in;uint8_t frame[244];size_t offset=0;
  while(offset<text.size()){
    size_t n=fragment(reinterpret_cast<const uint8_t*>(text.data()),text.size(),Channel::Command,0x1234,offset,frame,20);
    assert(n<=20&&frame[0]==0xa7&&frame[1]==1&&frame[2]==3&&read16(frame+4)==0x1234&&read16(frame+6)==offset);
    auto result=in.push(frame,n,uint32_t(offset),Channel::Command);offset+=n-kHeader;
    assert(result==(offset==text.size()?Assembly::Complete:Assembly::Partial));
  }
  assert(in.size()==text.size()&&!memcmp(in.data(),text.data(),text.size()));
  size_t n=fragment(reinterpret_cast<const uint8_t*>(text.data()),text.size(),Channel::Command,1,0,frame,20);
  assert(in.push(frame,n,0,Channel::Command)==Assembly::Partial);
  assert(in.push(frame,n,1,Channel::Command)==Assembly::Invalid); // duplicate START
  assert(in.push(frame,n,0,Channel::Command)==Assembly::Partial);
  write16(frame+6,12);frame[3]=0;
  assert(in.push(frame,n,2000,Channel::Command)==Assembly::Timeout);
  write16(frame+6,5);assert(in.push(frame,n,2001,Channel::Command)==Assembly::Invalid);
  frame[0]=0;assert(in.push(frame,n,0,Channel::Command)==Assembly::Invalid);
  std::string huge(kMaxCommand+1,'x');offset=0;bool invalid=false;
  while(offset<huge.size()){
    n=fragment(reinterpret_cast<const uint8_t*>(huge.data()),huge.size(),Channel::Command,2,offset,frame,sizeof(frame));
    auto result=in.push(frame,n,1,Channel::Command);offset+=n-kHeader;invalid|=result==Assembly::Invalid;
  }
  assert(invalid);
  const uint8_t valid[]={0xe5,0xbf,0x83,0xe7,0x8e,0x87};assert(validUtf8(valid,sizeof(valid)));
  const uint8_t bad[][4]={{0xc0,0x80,0,0},{0xed,0xa0,0x80,0},{0xf4,0x90,0x80,0x80},{0,0,0,0}};
  assert(!validUtf8(bad[0],2)&&!validUtf8(bad[1],3)&&!validUtf8(bad[2],4)&&!validUtf8(bad[3],1));
  ControlLease lease;assert(!lease.claim(0,0));assert(lease.claim(101,0));assert(!lease.claim(202,1));
  assert(!lease.renew(202,2)&&!lease.release(202));assert(lease.renew(101,10000));assert(lease.remaining(39999)==1);
  assert(!lease.expire(39999)&&lease.expire(40000));assert(!lease.owns(101));assert(lease.claim(202,40001));
  assert(lease.release(202)&&!lease.release(202));
  assert(lease.claim(303,UINT32_MAX-15000));assert(!lease.expire(14998));assert(lease.expire(14999));
  std::cout<<"WebBLE framing, UTF-8, bounds, deadlines and exclusive lease tests passed\n";
}
