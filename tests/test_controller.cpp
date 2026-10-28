#include "../firmware/RehabEsp32/src/protocol.h"
#include <assert.h>
#include <iostream>

void pulsesAndQueues() {
  rehab::Pulse pulse;
  assert(!pulse.tick(1000));
  pulse.start(rehab::PulseKind::Start,100,7);
  assert(pulse.high&&!pulse.tick(599));
  assert(pulse.tick(600)&&!pulse.high&&pulse.generation==7);
  assert(!pulse.tick(601)); // Completion is delivered only once.
  pulse.start(rehab::PulseKind::Stop,1000,8);
  assert(!pulse.tick(3999)&&pulse.high);
  assert(!pulse.tick(4000)&&!pulse.high); // 500 ms settling remains.
  assert(!pulse.tick(4499));
  assert(pulse.tick(4500)&&!pulse.high);
  assert(!pulse.tick(4501));
  pulse.start(rehab::PulseKind::Start,UINT32_MAX-100,9);
  assert(!pulse.tick(398)&&pulse.high);
  assert(pulse.tick(399)&&!pulse.high);

  rehab::Fifo<int,3> fifo;
  assert(fifo.push(1)&&fifo.push(2)&&fifo.push(3)&&!fifo.push(4));
  int value=-1; assert(fifo.pop(value)&&value==1);
  assert(fifo.push(4));
  for(int expected:{2,3,4}) assert(fifo.pop(value)&&value==expected);
  assert(!fifo.peek()&&!fifo.pop(value));
  fifo.push(5); fifo.clear(); assert(fifo.size()==0&&!fifo.pop(value));
}
void machineValues() {
  rehab::MachineValues values;
  const uint8_t data28[]={0x01,0x02,0x3B,0x03,0x04,0x05,0x06,0x50,0x01,0x00,0x07,0x08};
  const auto sample=rehab::makeFrame(0x28,data28,sizeof(data28));
  assert(values.accept(sample,1234)&&values.has28&&!values.has3f);
  assert(values.minutes==258&&values.seconds==59&&values.distance==772);
  assert(values.calories==1286&&values.bpm==80&&values.rpm==256&&values.watt28==1800);
  assert(values.at28==1234);
  const uint8_t data3f[]={0xFF,0xFF,0,0x20,0,0x30};
  assert(values.accept(rehab::makeFrame(0x3F,data3f,6),1500));
  assert(values.level==65535&&values.watt3f==32&&values.torque==48&&values.at3f==1500);
  // Malformed reports must not partially mutate either source's values.
  for(size_t size=0;size<sizeof(data28);++size)
    assert(!values.accept(rehab::makeFrame(0x28,data28,size),2000));
  assert(values.rpm==256&&values.at28==1234&&values.level==65535);
  for(size_t size=0;size<6;++size)
    assert(!values.accept(rehab::makeFrame(0x3F,data3f,size),2001));
  assert(values.level==65535&&values.at3f==1500);
}
int main(){pulsesAndQueues();machineValues();std::cout << "Pulse, FIFO and full-width machine fields passed\n";}
