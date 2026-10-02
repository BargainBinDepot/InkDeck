#pragma once
// =====================================================================
//  WebUI — WiFi + web page for uploading apps and files.
//  Only runs while the Uploader app is open.
//
//  Opening the Uploader joins the WiFi saved in /system/wifi.txt.
//  If there isn't one, or it can't connect, it scans so you can pick a
//  network on the device. A network is only saved once it connects.
//  The device's own hotspot is still available as a manual fallback.
// =====================================================================
#include <Arduino.h>
#include <vector>

namespace WebUI {

enum class Mode {
  Off,
  Scanning,     // looking for networks
  Idle,         // scan finished, not connected: pick a network
  Connecting,
  Station,      // connected to a WiFi network
  Hotspot       // running the device's own hotspot
};

struct Network {
  String ssid;
  int    rssi;       // dBm, higher (closer to 0) is stronger
  bool   open;       // no password needed
};

void start();                          // saved WiFi, else scan
void stop();                           // everything off
void loop();                           // call often while running

void startScan();                      // disconnects and rescans
void connectTo(const String& ssid, const String& password);   // saved on success
void startHotspot();

Mode   mode();
const std::vector<Network>& networks();  // strongest first, after a scan
String networkName();                    // SSID being joined / connected / hotspot name
String address();                        // IP as text, empty if not up
String notice();                         // last problem, e.g. wrong password ("" if none)
String lastEvent();
uint32_t filesReceived();
bool   changed();                        // true once after anything the device screen should show
bool   savedWifi(String& ssid, String& pass);   // WiFi saved in /system/wifi.txt

// A firmware update uploaded from the web page, waiting for confirmation on the device
bool   updatePending(String& version, uint32_t& size);
void   clearUpdate(bool removeFile);

}
