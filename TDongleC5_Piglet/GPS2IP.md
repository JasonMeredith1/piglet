# T-Dongle C5 GPS2IP BLE fork — v2.84-gps2ip.1

Adds phone GPS to the T-Dongle variant only. No external GPS module or phone
hotspot is needed for location. The original Piglet license and attribution
remain applicable. Based on upstream commit
`4e47f41f00ad2a63660d78f8db20bd33457a7b2c`.

## Setup

1. Build/flash **TDongleC5_Piglet**, using ESP32 Arduino core 3.3.10 and the
   dependencies in `.github/workflows/gps2ip-build.yml`. Enable PSRAM and use
   the huge-app partition. The existing committed files under `build/` are
   upstream binaries and **do not include GPS2IP**.
2. Put a FAT32 microSD card in the dongle. In the web configuration choose
   **GPS2IP phone via BLE**, save and restart. This is also the fork's default.
3. On the iPhone, enable GPS2IP's Bluetooth LE / Location & Navigation
   sharing and allow location access. Keep GPS2IP running and approve any
   iOS pairing request. This uses BLE service `1819` and notifications from
   Location & Speed `2A67`, as in the ADS-B dongle's BLE approach.
4. The dongle scans for LNS, pairs/bonds, subscribes, and reconnects after
   disconnects. With a blank name filter it picks the strongest LNS advertiser.
   If multiple phones are nearby, set `gps2ipDeviceName` to the phone's exact
   **advertised BLE name**, then restart. This is a name filter, not a unique
   device identity or security allowlist.
5. Confirm **BLE: LIVE** on the display and `gps2ipStatus` in `/status.json`.
   `SEARCHING`, `WAITING`, and `STALE/NO FIX` distinguish discovery,
   notification wait, and an unusable position. `WAIT UTC` means the position
   is fresh but the clock must be obtained before logging. There is no satellite count
   in this phone service; navigation shows BLE rather than fabricated bars.

Configuration in `/wardriver.cfg`:

```ini
gpsSource=gps2ip_ble
gps2ipDeviceName=
gps2ipTimeoutMs=5000
```

All three take effect after a restart. Timeout is clamped to 1000–30000 ms.
Use `gpsSource=uart` to restore the original wired GPS path (RX GPIO12,
TX GPIO11, `gpsBaud=9600` by default). There is no automatic source mixing.

## Logging behavior

- Phone fixes must include a valid coordinate pair and LNS **Position OK**.
  No-position, estimated, last-known, malformed, disconnected and expired
  fixes do not geotag new CSV records. The default expiry is five seconds.
- Networks still count as discovered while GPS is unavailable; those sightings
  are not queued for later geotagging. The original three-minute UART cache
  remains available only in wired mode.
- Coordinates, optional elevation, speed and heading feed the existing UI and
  CSV path. Missing elevation and horizontal accuracy are empty CSV fields;
  satellite counts and accuracy are not invented. Elevation is the value
  supplied by LNS; no altitude-datum correction is applied.
- CSV logging also waits for a valid UTC clock. The phone's optional LNS UTC
  field sets it; SNTP can set it when connected to the configured home Wi-Fi.
  If GPS2IP omits UTC, connect to home Wi-Fi once each boot to obtain time.
  Check `gpsClockReady` in `/status.json` if the fix is live but no rows appear.
- Phone GPS is available to normal scanning and mesh-core CSV logging. Mesh
  nodes continue to rely on their coordinator for geotagging.
- The Wardrive Go BLE bridge is disabled while GPS2IP is selected. Its normal
  behavior remains available in UART mode. Mesh page changes never tear down
  the phone's BLE connection.

## Validation

Host regression tests cover all 128 combinations of optional LNS fields,
every truncation boundary, negative coordinates and 24-bit elevation, range
checks, position status, calendar validation, disconnect/timeout, and millis
rollover. Run:

```sh
g++ -std=c++11 -Wall -Wextra -Werror tests/gps2ip_packet_test.cpp -o /tmp/gps2ip-test
/tmp/gps2ip-test
```

The `GPS2IP T-Dongle checks` workflow compiles the complete firmware and saves
fresh binaries plus these notes. A successful compile is not a hardware test.
Before field use verify pairing, background phone updates, reconnection,
stale-fix log suppression, both Wi-Fi bands, and mesh-page transitions on the
actual T-Dongle/iPhone. Radio coexistence may reduce scan throughput.
