#include "apptraverse/push_protocol.h"
#include <algorithm>
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
  for(auto const& value:std::vector<std::string>{"", "\xd0\x9f\xd1\x83\xd1\x88 \xf0\x9f\x94\x94", std::string(2048,'x')}){
    auto frame=Encode({1,UINT64_MAX,value});
    if(!Decode(frame,packet)||packet.value!=value||packet.request!=UINT64_MAX)return 6;
    for(std::size_t split=0;split<=frame.size();++split){
      Reader parts;int received=0;
      auto accept=[&](Packet const& p){if(p.value==value&&p.request==UINT64_MAX)++received;};
      if(!parts.Feed(Bytes(frame.begin(),frame.begin()+split),accept)||
         !parts.Feed(Bytes(frame.begin()+split,frame.end()),accept)||received!=1)return 7;
    }
  }
  if(!Encode({1,1,std::string(2049,'x')}).empty()||!Encode({4,1,""}).empty()||!Encode({129,1,"",3}).empty())return 8;
  for(auto const& invalid:std::vector<std::string>{"\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xf0\x9f","\x80"}){
    if(!Encode({1,1,invalid}).empty())return 9;
    auto frame=Encode({1,1,std::string(invalid.size(),'x')});
    std::copy(invalid.begin(),invalid.end(),frame.begin()+18);
    if(Decode(frame,packet))return 10;
  }
  for(unsigned field:{0,4,5,6,7,16}){
    auto malformed=ack;malformed[field]=field==5?0:255;
    if(Decode(malformed,packet))return 11;
    Reader recovering;int received=0;
    if(recovering.Feed(malformed,[&](auto const&){++received;}))return 12;
    if(!recovering.Feed(ack,[&](auto const&){++received;})||received!=1)return 13;
  }
  auto apple=Encode({3,42,"sandbox|org.apptraverse.SharedNodeChat.ios|aabb"});
  if(!Decode(apple,packet)||packet.method!=3||packet.request!=42)return 15;
  auto appleAck=Encode({0x83,42,"",0});
  if(!Decode(appleAck,packet)||packet.method!=0x83)return 16;
  Reader reopened;int received=0;
  reopened.Feed(Bytes(ack.begin(),ack.begin()+8),[&](auto const&){++received;});
  reopened.Reset();
  if(!reopened.Feed(ack,[&](auto const&){++received;})||received!=1)return 14;
  std::cout<<"C++/Java wire fixture and stream framing PASS\n";
}
