#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Push API v1. No native structs/UUID layouts are copied onto the wire.
namespace apptraverse::push {
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::size_t kMaxFrame = 4096;
struct Packet {
  std::uint8_t method{}; // 1 register, 2 notify, 0x81/0x82 response
  std::uint64_t request{};
  std::string value; // token for register; target UID for notify
  std::uint8_t status{}; // response: 0 registered/provider accepted; 1 rejected; 2 provider error
};
inline void Number(Bytes& b, std::uint64_t n, unsigned size) {
  for (unsigned i=size; i>0; --i) b.push_back(static_cast<std::uint8_t>(n >> ((i-1)*8)));
}
inline Bytes Encode(Packet const& p) {
  if (p.value.size()>2048) return {};
  Bytes b{'A','T','P','N',0,0,1,p.method}; Number(b,p.request,8);
  if (p.method & 0x80) b.push_back(p.status);
  else { Number(b,p.value.size(),2); b.insert(b.end(),p.value.begin(),p.value.end()); }
  auto n=b.size(); b[4]=static_cast<std::uint8_t>(n>>8); b[5]=static_cast<std::uint8_t>(n); return b;
}
inline bool Decode(Bytes const& b, Packet& p) {
  if (b.size()<17 || b.size()>kMaxFrame || b[0]!='A'||b[1]!='T'||b[2]!='P'||b[3]!='N'||b[6]!=1 ||
      ((std::size_t(b[4])<<8)|b[5])!=b.size()) return false;
  p={}; p.method=b[7];
  for (unsigned i=8;i<16;++i) p.request=(p.request<<8)|b[i];
  if(p.method==0x81||p.method==0x82) {if(b.size()!=17)return false;p.status=b[16];return true;}
  if((p.method!=1&&p.method!=2)||b.size()<18) return false;
  auto n=(std::size_t(b[16])<<8)|b[17];
  if(n>2048||n+18!=b.size())return false;
  p.value.assign(b.begin()+18,b.end()); return true;
}
// Aether transports may split/coalesce application frames.
class Reader {
 public:
  template<class F> bool Feed(Bytes const& chunk,F receive) {
    for(auto c:chunk) {
      bytes_.push_back(c);
      if(bytes_.size()==6) {
        size_=(std::size_t(bytes_[4])<<8)|bytes_[5];
        if(size_<17||size_>kMaxFrame){Reset();return false;}
      }
      if(size_ && bytes_.size()==size_) {
        Packet p; bool ok=Decode(bytes_,p); Reset(); if(!ok)return false; receive(p);
      }
    }
    return true;
  }
  void Reset(){bytes_.clear();size_=0;}
 private: Bytes bytes_; std::size_t size_{};
};
}
