#include "clock.h"
#include "config.h"
#include "storage.h"
#include "webui.h"
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"

namespace Clock {

static const uint32_t MIN_VALID = 1704067200;   // 2024-01-01: anything earlier means "not set"
static const char* src = "not set";
static int rtcType = 0;                         // 0 = none, 1 = DS3231, 2 = PCF8563
static String tzStr = DEFAULT_TZ;
static bool h12 = false;

struct Zone { const char* label; const char* posix; };
static const Zone ZONES[] = {
  { "Pacific",              "PST8PDT,M3.2.0,M11.1.0" },
  { "Mountain",             "MST7MDT,M3.2.0,M11.1.0" },
  { "Mountain (no DST)",    "MST7" },
  { "Central",              "CST6CDT,M3.2.0,M11.1.0" },
  { "Saskatchewan",         "CST6" },
  { "Eastern",              "EST5EDT,M3.2.0,M11.1.0" },
  { "Atlantic",             "AST4ADT,M3.2.0,M11.1.0" },
  { "Newfoundland",         "NST3:30NDT,M3.2.0,M11.1.0" },
  { "UK / Ireland",         "GMT0BST,M3.5.0/1,M10.5.0" },
  { "Central Europe",       "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Eastern Australia",    "AEST-10AEDT,M10.1.0,M4.1.0/3" },
  { "UTC",                  "UTC0" },
};
int zoneCount()              { return sizeof(ZONES) / sizeof(ZONES[0]); }
const char* zoneLabel(int i) { return (i >= 0 && i < zoneCount()) ? ZONES[i].label : "Custom"; }
const char* zonePosix(int i) { return (i >= 0 && i < zoneCount()) ? ZONES[i].posix : ""; }
int currentZone() {
  for (int i = 0; i < zoneCount(); i++) if (tzStr == ZONES[i].posix) return i;
  return -1;
}
static volatile bool ntpArrived = false;

enum class Sync { Idle, Connecting, WaitNtp };
static Sync sync = Sync::Idle;
static uint32_t syncT0 = 0;
static String syncMsg;

// ---------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------
static uint8_t fromBcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t toBcd(uint8_t v)   { return ((v / 10) << 4) | (v % 10); }

static bool probe(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

static bool readRegs(uint8_t a, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(a);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(a, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool writeRegs(uint8_t a, uint8_t reg, const uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(a);
  Wire.write(reg);
  for (uint8_t i = 0; i < n; i++) Wire.write(buf[i]);
  return Wire.endTransmission() == 0;
}

// Days-from-civil (UTC), so we don't depend on timegm()
static uint32_t toEpoch(int y, int mo, int d, int h, int mi, int s) {
  y -= mo <= 2;
  const int era = y / 400;
  const unsigned yoe = y - era * 400;
  const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = era * 146097L + doe - 719468;
  return days * 86400UL + h * 3600UL + mi * 60UL + s;
}

bool valid() { return time(nullptr) > (time_t)MIN_VALID; }

// ---------------------------------------------------------------------
//  RTC chips (both store UTC here)
// ---------------------------------------------------------------------
static bool rtcRead(uint32_t& utc) {
  uint8_t r[7];
  if (rtcType == 1) {                                     // DS3231
    uint8_t status;
    if (!readRegs(0x68, 0x0F, &status, 1) || (status & 0x80)) return false;   // oscillator stopped: invalid
    if (!readRegs(0x68, 0x00, r, 7)) return false;
    utc = toEpoch(2000 + fromBcd(r[6]), fromBcd(r[5] & 0x1F), fromBcd(r[4] & 0x3F),
                  fromBcd(r[2] & 0x3F), fromBcd(r[1] & 0x7F), fromBcd(r[0] & 0x7F));
  } else if (rtcType == 2) {                              // PCF8563
    if (!readRegs(0x51, 0x02, r, 7) || (r[0] & 0x80)) return false;           // voltage-low flag: invalid
    utc = toEpoch(2000 + fromBcd(r[6]), fromBcd(r[5] & 0x1F), fromBcd(r[3] & 0x3F),
                  fromBcd(r[2] & 0x3F), fromBcd(r[1] & 0x7F), fromBcd(r[0] & 0x7F));
  } else {
    return false;
  }
  return utc > MIN_VALID;
}

static void rtcWrite(uint32_t utc) {
  if (!rtcType) return;
  time_t t = utc;
  struct tm g;
  gmtime_r(&t, &g);
  const uint8_t yy = (g.tm_year + 1900) % 100;
  if (rtcType == 1) {
    const uint8_t r[7] = { toBcd(g.tm_sec), toBcd(g.tm_min), toBcd(g.tm_hour), (uint8_t)(g.tm_wday + 1),
                           toBcd(g.tm_mday), toBcd(g.tm_mon + 1), toBcd(yy) };
    writeRegs(0x68, 0x00, r, 7);
    uint8_t status = 0;
    if (readRegs(0x68, 0x0F, &status, 1)) { status &= ~0x80; writeRegs(0x68, 0x0F, &status, 1); }  // clear "stopped"
  } else {
    const uint8_t r[7] = { toBcd(g.tm_sec), toBcd(g.tm_min), toBcd(g.tm_hour), toBcd(g.tm_mday),
                           (uint8_t)g.tm_wday, toBcd(g.tm_mon + 1), toBcd(yy) };
    writeRegs(0x51, 0x02, r, 7);
  }
  Serial.printf("[clock] RTC updated\n");
}

// ---------------------------------------------------------------------
//  Timezone
// ---------------------------------------------------------------------
static void applyTz() {
  setenv("TZ", tzStr.c_str(), 1);
  tzset();
}

String tz() { return tzStr; }

static bool saveConfig() {
  return Storage::writeText(TIME_CONFIG_FILE, "tz=" + tzStr + "\nclock=" + String(h12 ? "12" : "24") + "\n");
}

bool setTz(const String& posix) {
  String t = posix;
  t.trim();
  if (!t.length() || t.length() > 60) return false;
  tzStr = t;
  applyTz();
  return saveConfig();
}

bool use12h() { return h12; }
bool set12h(bool on) { h12 = on; return saveConfig(); }

// ---------------------------------------------------------------------
//  Setting the time
// ---------------------------------------------------------------------
bool setEpoch(uint32_t utc, const char* from) {
  if (utc < MIN_VALID) return false;
  struct timeval tv = { (time_t)utc, 0 };
  settimeofday(&tv, nullptr);
  src = from;
  rtcWrite(utc);
  Serial.printf("[clock] time set from %s: %s\n", from, dateLine().c_str());
  return true;
}

static void onNtp(struct timeval*) { ntpArrived = true; }

void ntpStart() {
  ntpArrived = false;
  sntp_set_time_sync_notification_cb(onNtp);
  configTzTime(tzStr.c_str(), "pool.ntp.org", "time.google.com");
  Serial.println("[clock] asking the internet for the time");
}

static void stopOwnWifi() {
  if (WebUI::mode() != WebUI::Mode::Off) return;     // the Uploader owns WiFi now
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void syncNow() {
  if (sync != Sync::Idle) return;
  if (WebUI::mode() != WebUI::Mode::Off) { syncMsg = "The Uploader is using WiFi"; return; }
  String ssid, pass;
  if (!WebUI::savedWifi(ssid, pass)) { syncMsg = "No WiFi saved yet (use the Uploader)"; return; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  sync = Sync::Connecting;
  syncT0 = millis();
  syncMsg = "Connecting to " + ssid;
  Serial.printf("[clock] %s\n", syncMsg.c_str());
}

String syncStatus() { return syncMsg; }
bool busy() { return sync != Sync::Idle; }

// ---------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------
void begin() {
  // Timezone saved from the web page, if any
  String cfg;
  if (Storage::readText(TIME_CONFIG_FILE, cfg, 256)) {
    int p = cfg.indexOf("tz=");
    if (p >= 0) {
      int e = cfg.indexOf('\n', p);
      String t = cfg.substring(p + 3, e < 0 ? cfg.length() : e);
      t.trim();
      if (t.length()) tzStr = t;
    }
    h12 = cfg.indexOf("clock=12") >= 0;
  }
  applyTz();

  if (probe(0x68))      rtcType = 1;
  else if (probe(0x51)) rtcType = 2;
  Serial.printf("[clock] RTC: %s\n", rtcType ? rtcName() : "none (time is lost at power-off)");

  if (valid()) {
    src = "before reset";
  } else {
    uint32_t utc;
    if (rtcRead(utc)) {
      struct timeval tv = { (time_t)utc, 0 };
      settimeofday(&tv, nullptr);
      src = "RTC";
    }
  }
  Serial.printf("[clock] time: %s (%s)\n", dateLine().c_str(), src);

#if AUTO_TIME_SYNC
  if (!valid()) syncNow();                     // unknown time: try the saved WiFi in the background
#endif
}

void loop() {
  if (ntpArrived) {
    ntpArrived = false;
    src = "internet";
    rtcWrite(time(nullptr));
    Serial.printf("[clock] internet time: %s\n", dateLine().c_str());
    if (sync == Sync::WaitNtp) { syncMsg = "Time set from the internet"; sync = Sync::Idle; stopOwnWifi(); }
  }

  if (sync == Sync::Connecting) {
    if (WebUI::mode() != WebUI::Mode::Off) { sync = Sync::Idle; syncMsg = ""; return; }   // Uploader took over
    if (WiFi.status() == WL_CONNECTED) {
      ntpStart();
      sync = Sync::WaitNtp;
      syncT0 = millis();
      syncMsg = "Getting the time...";
    } else if (millis() - syncT0 > 15000) {
      sync = Sync::Idle;
      syncMsg = "Couldn't join WiFi";
      stopOwnWifi();
    }
  } else if (sync == Sync::WaitNtp && millis() - syncT0 > 12000) {
    sync = Sync::Idle;
    syncMsg = "No answer from the time server";
    stopOwnWifi();
  }
}

// ---------------------------------------------------------------------
//  Display
// ---------------------------------------------------------------------
String hhmm() {
  if (!valid()) return "--:--";
  time_t t = time(nullptr);
  struct tm l;
  localtime_r(&t, &l);
  char b[12];
  if (h12) snprintf(b, sizeof(b), "%d:%02d %s", l.tm_hour % 12 ? l.tm_hour % 12 : 12, l.tm_min, l.tm_hour < 12 ? "AM" : "PM");
  else     snprintf(b, sizeof(b), "%02d:%02d", l.tm_hour, l.tm_min);
  return String(b);
}

String dateLine() {
  if (!valid()) return "unknown";
  time_t t = time(nullptr);
  struct tm l;
  localtime_r(&t, &l);
  char b[32];
  strftime(b, sizeof(b), "%a %d %b %Y ", &l);
  return String(b) + hhmm();
}

const char* source()  { return src; }
const char* rtcName() { return rtcType == 1 ? "DS3231" : rtcType == 2 ? "PCF8563" : nullptr; }

}
