#pragma once
// =====================================================================
//  Clock — real time of day.
//
//  Sources, best first:
//    - A battery-backed RTC module on the I2C bus (DS3231 at 0x68 or
//      PCF8563 at 0x51). Keeps time while the device is off.
//    - Internet time (NTP) whenever WiFi is up: automatically at boot if
//      the time is unknown and WiFi is saved, whenever the Uploader
//      connects, or on demand from the System app. Written back to the RTC.
//    - The web page's "Set time from this browser" button.
//  Without an RTC the ESP32 keeps time across resets but not power-off.
// =====================================================================
#include <Arduino.h>

namespace Clock {

void begin();                   // after Wire and the SD card are up
void loop();

bool   valid();                 // do we know the real time?
String hhmm();                  // "14:05", or "--:--" when unknown
String dateLine();              // "Sat 26 Sep 2026 14:05", or "unknown"
const char* source();           // where the current time came from
const char* rtcName();          // "DS3231", "PCF8563" or nullptr

bool setEpoch(uint32_t utc, const char* from);   // set the time (and the RTC)
void ntpStart();                // WiFi is up: ask the internet for the time
void syncNow();                 // join the saved WiFi briefly and get the time
String syncStatus();            // what that last attempt did
bool   busy();                  // a background sync is in progress

String tz();                    // POSIX timezone string
bool   setTz(const String& posix);

bool   use12h();                // 12-hour clock ("2:05 PM") instead of 24-hour ("14:05")
bool   set12h(bool on);

// Built-in time zone list (the same one the web page offers)
int         zoneCount();
const char* zoneLabel(int i);
const char* zonePosix(int i);
int         currentZone();      // index in the list, or -1 for a custom zone

}
