#include "../TDongleC5_Piglet/Gps2ipPacket.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <cstdio>

static void le(std::vector<uint8_t>& p, int64_t v, unsigned n) {
  for (unsigned i=0; i<n; ++i) p.push_back((uint64_t(v) >> (8*i)) & 255);
}
static std::vector<uint8_t> packet(unsigned flags) {
  std::vector<uint8_t> p; le(p,flags,2);
  if (flags&1) le(p,1250,2); // 12.5 m/s = 45 km/h
  if (flags&2) le(p,0x123456,3);
  if (flags&4) { le(p,295000000,4); le(p,-985000000,4); }
  if (flags&8) le(p,-1234,3); // signed 24-bit, -12.34 metres
  if (flags&16) le(p,27000,2);
  if (flags&32) le(p,77,1);
  if (flags&64) { le(p,2026,2); for(int v : {11,1,12,34,56}) p.push_back(v); }
  return p;
}
int main() {
  Gps2ipFix f;
  // Every optional-field combination, including distance/rolling-time skips.
  for (unsigned bits=0; bits<128; ++bits) {
    auto p=packet(bits | 0x80);
    assert(gps2ipDecode(p.data(),p.size(),f));
    assert(f.position == bool(bits&4));
    assert(f.hasSpeed == bool(bits&1));
    if (bits&4) { assert(std::abs(f.lat-29.5)<1e-8); assert(std::abs(f.lon+98.5)<1e-8); }
    if (bits&1) assert(std::abs(f.speedKmph-45)<1e-8);
    if (bits&8) assert(std::abs(f.altitude+12.34)<1e-8);
    if (bits&16) assert(std::abs(f.heading-270)<1e-8);
    assert(f.hasUtc == bool(bits&64));
    // Every possible truncated length must be rejected, with output untouched.
    for (size_t n=0;n<p.size();++n) {
      Gps2ipFix sentinel; sentinel.lat=77;
      assert(!gps2ipDecode(p.data(),n,sentinel)); assert(sentinel.lat==77);
    }
  }
  for(unsigned status : {0,2,3}) {
    auto p=packet(4 | (status<<7));
    assert(gps2ipDecode(p.data(),p.size(),f)); assert(!f.position);
  }
  auto p=packet(0x84); p[2]=255;p[3]=255;p[4]=255;p[5]=127;
  assert(!gps2ipDecode(p.data(),p.size(),f)); // latitude outside range
  p=packet(0x84); le(p,1,1); assert(!gps2ipDecode(p.data(),p.size(),f));
  assert(!gps2ipDecode(nullptr,0,f));
  p=packet(0xc4); p[p.size()-5]=2; p[p.size()-4]=30;
  assert(gps2ipDecode(p.data(),p.size(),f) && !f.hasUtc); // Feb 30
  p=packet(0x84); assert(gps2ipDecode(p.data(),p.size(),f));
  assert(gps2ipFixFresh(true,true,f,5999,1000,5000));
  assert(!gps2ipFixFresh(true,true,f,6000,1000,5000));
  assert(!gps2ipFixFresh(false,true,f,1001,1000,5000));
  assert(!gps2ipFixFresh(true,false,f,1001,1000,5000));
  assert(gps2ipFixFresh(true,true,f,10,0xfffffff0U,5000)); // millis rollover
  puts("GPS2IP: 128 field combinations, truncation, signed values, status, UTC and freshness tests passed");
}
