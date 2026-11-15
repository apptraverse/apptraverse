#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Push API v1. No native structs/UUID layouts are copied onto the wire.
namespace apptraverse::push {
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::size_t kMaxFrame = 4096;
inline constexpr std::size_t kMaxValue = 2048;
// Match Java's strict UTF-8 decoder: reject truncation, overlong encodings,
// surrogate code points and values outside Unicode's scalar range.
inline bool ValidUtf8(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    auto first = static_cast<unsigned char>(text[i++]);
    if (first < 0x80) continue;
    unsigned count;
    std::uint32_t value, minimum;
    if (first >= 0xc2 && first <= 0xdf) {
      count = 1; value = first & 0x1f; minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      count = 2; value = first & 0x0f; minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      count = 3; value = first & 0x07; minimum = 0x10000;
    } else return false;
    if (text.size() - i < count) return false;
    while (count--) {
      auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80) return false;
      value = (value << 6) | (next & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
  }
  return true;
}
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
  if (p.method!=1&&p.method!=2&&p.method!=0x81&&p.method!=0x82) return {};
  if (p.value.size()>kMaxValue||!ValidUtf8(p.value)) return {};
  if ((p.method&0x80)&&p.status>2) return {};
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
  if(p.method==0x81||p.method==0x82) {if(b.size()!=17||b[16]>2)return false;p.status=b[16];return true;}
  if((p.method!=1&&p.method!=2)||b.size()<18) return false;
  auto n=(std::size_t(b[16])<<8)|b[17];
  if(n>kMaxValue||n+18!=b.size())return false;
  p.value.assign(b.begin()+18,b.end()); return ValidUtf8(p.value);
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
