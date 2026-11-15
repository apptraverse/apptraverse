#include "apptraverse/push_protocol.h"
#include <iostream>
#include <iomanip>
int main(){
  using namespace apptraverse::push;
  auto bytes=Encode({1,0x0102030405060708ULL,"abc"});
  Bytes expected{0x41,0x54,0x50,0x4e,0,21,1,1,1,2,3,4,5,6,7,8,0,3,0x61,0x62,0x63};
  if(bytes!=expected)return 1;
  Reader reader;int count=0;
  for(auto b:bytes)if(!reader.Feed({b},[&](Packet const& p){if(p.value=="abc"&&p.request==0x0102030405060708ULL)++count;}))return 2;
  bytes.insert(bytes.end(),expected.begin(),expected.end());
  if(!reader.Feed(bytes,[&](Packet const&){++count;})||count!=3)return 3;
  Packet packet;expected[6]=2;if(Decode(expected,packet))return 4;
  auto ack=Encode({0x81,9,"",0});if(!Decode(ack,packet)||packet.request!=9||packet.status!=0)return 5;
  std::cout<<"C++/Java wire fixture and stream framing PASS\n";
}
