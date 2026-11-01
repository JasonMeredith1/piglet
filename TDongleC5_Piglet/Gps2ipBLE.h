#pragma once
#include <NimBLEDevice.h>
#include "Gps2ipPacket.h"

// GPS2IP owns NimBLE in phone mode. The optional Wardrive Go BLE server
// stays disabled in that mode, including across mesh page transitions.
static portMUX_TYPE gps2ipMux = portMUX_INITIALIZER_UNLOCKED;
static Gps2ipFix gps2ipPending, gps2ipCurrent;
static uint32_t gps2ipReceivedAt = 0, gps2ipCurrentAt = 0;
static bool gps2ipReceived = false, gps2ipConnected = false;
static bool gps2ipCurrentReceived = false, gps2ipCurrentConnected = false;
static String gps2ipNameFilter;
static bool gps2ipRunning = false;

static void gps2ipNotify(NimBLERemoteCharacteristic*, uint8_t* bytes, size_t len, bool) {
  Gps2ipFix fix;
  if (!gps2ipDecode(bytes, len, fix)) return;
  portENTER_CRITICAL(&gps2ipMux);
  gps2ipPending = fix;
  gps2ipReceivedAt = millis();
  gps2ipReceived = true;
  portEXIT_CRITICAL(&gps2ipMux);
}

static void gps2ipSetConnected(bool connected) {
  portENTER_CRITICAL(&gps2ipMux);
  gps2ipConnected = connected;
  gps2ipReceived = false; // reconnect must receive a new position
  portEXIT_CRITICAL(&gps2ipMux);
}

class Gps2ipClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*, int) override { gps2ipSetConnected(false); }
};
static Gps2ipClientCallbacks gps2ipCallbacks;

static void gps2ipWorker(void*) {
  NimBLEDevice::init("Piglet-GPS2IP");
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(true, false, true); // bond, no display/passkey
  NimBLEClient* client = NimBLEDevice::createClient();
  if (!client) { vTaskDelete(nullptr); return; }
  client->setClientCallbacks(&gps2ipCallbacks, false);
  client->setConnectTimeout(5000);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(160);
  scan->setWindow(48); // leave airtime for dual-band Wi-Fi scans
  scan->setMaxResults(32);
  const NimBLEUUID serviceUuid(uint16_t(0x1819));
  for (;;) {
    if (!client->isConnected()) {
      gps2ipSetConnected(false);
      NimBLEScanResults results = scan->getResults(4000, false);
      const NimBLEAdvertisedDevice* chosen = nullptr;
      for (int i = 0; i < results.getCount(); ++i) {
        const auto* device = results.getDevice(i);
        if (!device->isAdvertisingService(serviceUuid)) continue;
        if (gps2ipNameFilter.length() && device->getName() != gps2ipNameFilter.c_str()) continue;
        if (!chosen || device->getRSSI() > chosen->getRSSI()) chosen = device;
      }
      if (chosen && client->connect(chosen)) {
        // Pair first, as on the ADS-B dongle, then discover/subscribe to LNS.
        if (client->secureConnection()) {
          auto* service = client->getService(serviceUuid);
          auto* location = service ? service->getCharacteristic(NimBLEUUID(uint16_t(0x2A67))) : nullptr;
          // Set state before subscribing: a notification may arrive immediately.
          gps2ipSetConnected(true);
          if (!location || !location->canNotify() || !location->subscribe(true, gps2ipNotify)) {
            gps2ipSetConnected(false);
            client->disconnect();
          } else Serial.println("[GPS2IP] Paired; Location & Speed subscribed");
        } else client->disconnect();
      }
      scan->clearResults();
      vTaskDelay(pdMS_TO_TICKS(3000));
    } else vTaskDelay(pdMS_TO_TICKS(250));
  }
}

static void gps2ipBegin(const String& nameFilter) {
  if (gps2ipRunning) return;
  gps2ipNameFilter = nameFilter; // immutable while task is running
  gps2ipRunning = xTaskCreate(gps2ipWorker, "gps2ip", 6144, nullptr, 1, nullptr) == pdPASS;
  if (!gps2ipRunning) Serial.println("[GPS2IP] ERROR: unable to start BLE task");
}

// Only the main Arduino task accesses gps2ipCurrent. The notification/worker
// tasks publish under a short critical section, with no heap allocation there.
static void gps2ipPoll() {
  portENTER_CRITICAL(&gps2ipMux);
  gps2ipCurrent = gps2ipPending;
  gps2ipCurrentAt = gps2ipReceivedAt;
  gps2ipCurrentReceived = gps2ipReceived;
  gps2ipCurrentConnected = gps2ipConnected;
  portEXIT_CRITICAL(&gps2ipMux);
}

static bool gps2ipFresh(uint32_t timeout) {
  return gps2ipFixFresh(gps2ipCurrentConnected, gps2ipCurrentReceived, gps2ipCurrent,
                        millis(), gps2ipCurrentAt, timeout);
}
