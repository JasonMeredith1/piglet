#pragma once
#include <stdint.h>
#include <stddef.h>

// Bluetooth SIG Location and Speed (0x2A67), little endian. Optional fields
// must be walked in flag order; never cast the unaligned notification buffer.
struct Gps2ipFix {
  double lat = 0, lon = 0, altitude = 0, speedKmph = 0, heading = 0;
  bool position = false, hasAltitude = false, hasSpeed = false, hasHeading = false;
  bool hasUtc = false;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

static bool gps2ipDecode(const uint8_t* p, size_t n, Gps2ipFix& out) {
  if (!p || n < 2) return false;
  const uint16_t flags = uint16_t(p[0]) | (uint16_t(p[1]) << 8);
  size_t needed = 2;
  const uint8_t widths[] = {2, 3, 8, 3, 2, 1, 7};
  for (unsigned i = 0; i < 7; ++i) if (flags & (1u << i)) needed += widths[i];
  if (n != needed || (flags & 0xE000)) return false;
  size_t at = 2;
  auto u16 = [&]() -> uint16_t { uint16_t v = p[at] | (uint16_t(p[at+1]) << 8); at += 2; return v; };
  auto s32 = [&]() -> int32_t {
    uint32_t v = uint32_t(p[at]) | (uint32_t(p[at+1]) << 8) |
                 (uint32_t(p[at+2]) << 16) | (uint32_t(p[at+3]) << 24);
    at += 4;
    return v <= 0x7fffffffU ? int32_t(v) : int32_t(int64_t(v) - 0x100000000LL);
  };
  Gps2ipFix f;
  if (flags & 1) { f.speedKmph = u16() * 0.036; f.hasSpeed = true; }
  if (flags & 2) at += 3; // total distance
  if (flags & 4) {
    f.lat = s32() * 1e-7; f.lon = s32() * 1e-7;
    if (f.lat < -90 || f.lat > 90 || f.lon < -180 || f.lon > 180) return false;
    // Reject no-position, estimated and last-known positions for logging.
    f.position = ((flags >> 7) & 3) == 1;
  }
  if (flags & 8) {
    int32_t v = int32_t(p[at]) | (int32_t(p[at+1]) << 8) | (int32_t(p[at+2]) << 16);
    at += 3; if (v & 0x800000) v -= 0x1000000;
    f.altitude = v * 0.01; f.hasAltitude = true;
  }
  if (flags & 16) { f.heading = u16() * 0.01; f.hasHeading = f.heading < 360; }
  if (flags & 32) ++at;
  if (flags & 64) {
    f.year = u16(); f.month = p[at++]; f.day = p[at++];
    f.hour = p[at++]; f.minute = p[at++]; f.second = p[at++];
    const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = f.year % 4 == 0 && (f.year % 100 != 0 || f.year % 400 == 0);
    unsigned maxDay = f.month >= 1 && f.month <= 12 ? days[f.month-1] + (f.month == 2 && leap) : 0;
    f.hasUtc = f.year >= 2020 && f.year <= 2099 && f.day >= 1 && f.day <= maxDay &&
               f.hour < 24 && f.minute < 60 && f.second < 60;
  }
  out = f;
  return true;
}

static bool gps2ipFixFresh(bool connected, bool received, const Gps2ipFix& fix,
                           uint32_t now, uint32_t receivedAt, uint32_t timeout) {
  return connected && received && fix.position && uint32_t(now - receivedAt) < timeout;
}
