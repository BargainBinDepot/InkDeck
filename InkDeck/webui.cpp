#include "webui.h"
#include "config.h"
#include "storage.h"
#include "luahost.h"
#include "screen.h"
#include "keyboard.h"
#include "webpage.h"
#include "clock.h"
#include "ota.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <algorithm>

namespace WebUI {

static WebServer server(80);
static Mode curMode = Mode::Off;
static String netName, lastEv, noticeMsg;
static String pendingSsid, pendingPass;      // saved to wifi.txt only once the connection works
static bool pendingSave = false;
static std::vector<Network> nets;
static bool scanning = false;
static uint32_t received = 0, t0 = 0;
static bool dirtyFlag = false, serverUp = false, mdnsUp = false, routesSet = false;

// Upload in progress
static File upFile;
static String upPath, upTmp, upErr;
static bool upOk = false;

static void event(const String& s) {
  lastEv = s;
  dirtyFlag = true;
  Serial.printf("[web] %s\n", s.c_str());
}

// =====================================================================
//  Helpers
// =====================================================================
static String jstr(const String& s) {
  String o = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o + "\"";
}

// Only these folders can be listed / uploaded to / downloaded from / deleted.
// /system stays private (it holds the WiFi password).
static bool allowedPath(const String& p, bool allowRoot) {
  if (!p.startsWith("/") || p.indexOf("..") >= 0 || p.indexOf("//") >= 0 || p.endsWith("/")) return false;
  const char* roots[] = { APPS_DIR, NOTES_DIR };
  for (const char* r : roots) {
    const String R(r);
    if (p == R) return allowRoot;
    if (p.startsWith(R + "/")) return true;
  }
  return false;
}

static void sendJson(const String& body)    { server.send(200, "application/json", body); }
static void sendErr(int code, const char* m) { server.send(code, "text/plain", m); }

static bool readWifi(String& ssid, String& pass);
bool savedWifi(String& ssid, String& pass) { return readWifi(ssid, pass); }
static bool readWifi(String& ssid, String& pass) {
  String t;
  if (!Storage::readText(WIFI_CONFIG_FILE, t, 256)) return false;
  const int nl = t.indexOf('\n');
  ssid = nl < 0 ? t : t.substring(0, nl);
  pass = nl < 0 ? String("") : t.substring(nl + 1);
  const int nl2 = pass.indexOf('\n');
  if (nl2 >= 0) pass = pass.substring(0, nl2);
  ssid.trim();
  pass.replace("\r", "");
  return ssid.length() > 0;
}

static String hotspotName() {
  String mac = WiFi.macAddress();        // AA:BB:CC:DD:EE:FF
  mac.replace(":", "");
  return String(BRAND_NAME) + "-" + mac.substring(mac.length() - 4);
}

// =====================================================================
//  Routes
// =====================================================================
static void hIndex() { server.send_P(200, "text/html", INDEX_HTML); }

static void hTabs() {
  String j = "[{\"id\":\"apps\",\"label\":\"Apps\",\"type\":\"apps\"}";
  // Notes: a plain-text editor in the browser
  j += ",{\"id\":\"notes\",\"label\":\"Notes\",\"type\":\"editor\",\"format\":\"text\",\"path\":\"" NOTES_DIR
       "\",\"ext\":\".txt\",\"accept\":\".txt\"}";
  for (const auto& a : LuaHost::readManifests()) {
    if (!a.tabLabel.length()) continue;
    const String dir = String(APPS_DIR) + "/" + a.id + "/" + a.tabDir;
    if (a.tabEditor.length()) {
      // A browser editor for the app's documents
      const bool md = a.tabEditor == "markdown";
      j += ",{\"id\":" + jstr("app-" + a.id) + ",\"label\":" + jstr(a.tabLabel) +
           ",\"type\":\"editor\",\"format\":\"" + String(md ? "markdown" : "text") + "\",\"path\":" + jstr(dir) +
           ",\"ext\":\"" + String(md ? ".md" : ".txt") + "\",\"accept\":" + jstr(a.tabAccept.length() ? a.tabAccept : String(md ? ".md,.txt" : ".txt")) + "}";
    } else if (a.tabPage.length()) {
      // The app ships its own web page; it's shown inside the tab
      j += ",{\"id\":" + jstr("app-" + a.id) + ",\"label\":" + jstr(a.tabLabel) +
           ",\"type\":\"page\",\"url\":" + jstr("/app/" + a.id + "/" + a.tabPage) +
           ",\"path\":" + jstr(dir) + "}";
    } else {
      j += ",{\"id\":" + jstr("app-" + a.id) + ",\"label\":" + jstr(a.tabLabel) +
           ",\"type\":\"folder\",\"path\":" + jstr(dir) +
           ",\"accept\":" + jstr(a.tabAccept) + "}";
    }
  }
  j += ",{\"id\":\"system\",\"label\":\"System\",\"type\":\"system\"}]";
  sendJson(j);
}

static void hApps() {
  String j = "[";
  bool first = true;
  for (const auto& a : LuaHost::readManifests()) {
    if (!first) j += ",";
    first = false;
    const bool hasImage = a.image.length() && Storage::exists(String(APPS_DIR) + "/" + a.id + "/" + a.image);
    j += "{\"id\":" + jstr(a.id) + ",\"name\":" + jstr(a.name) + ",\"icon\":" + jstr(a.icon) +
         ",\"image\":" + jstr(hasImage ? "/app/" + a.id + "/" + a.image : String("")) +
         ",\"version\":" + jstr(a.version) + ",\"author\":" + jstr(a.author) +
         ",\"description\":" + jstr(a.description) + ",\"tab\":" + jstr(a.tabLabel) + "}";
  }
  sendJson(j + "]");
}

static void hList() {
  const String dir = server.arg("dir");
  if (!allowedPath(dir, true)) return sendErr(403, "folder not allowed");
  Storage::mkdirs(dir);                          // app tab folders may not exist yet
  String j = "[";
  bool first = true;
  for (const auto& e : Storage::list(dir)) {
    if (!first) j += ",";
    first = false;
    j += "{\"name\":" + jstr(e.name) + ",\"dir\":" + String(e.dir ? "true" : "false") +
         ",\"size\":" + String(e.size) + "}";
  }
  sendJson(j + "]");
}

static void hDownload() {
  const String p = server.arg("path");
  if (!allowedPath(p, false) || !Storage::exists(p) || Storage::isDir(p)) return sendErr(404, "not found");
  File f = SD.open(p.c_str(), FILE_READ);
  if (!f) return sendErr(500, "can't open file");
  const String name = p.substring(p.lastIndexOf('/') + 1);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void hDelete() {
  const String p = server.arg("path");
  if (!allowedPath(p, false)) return sendErr(403, "path not allowed");
  if (!Storage::exists(p)) return sendErr(404, "not found");
  if (!Storage::removeTree(p)) return sendErr(500, "delete failed");
  event("Deleted " + p);
  sendJson("{\"ok\":true}");
}

static void hRename() {
  const String from = server.arg("from"), to = server.arg("to");
  if (!allowedPath(from, false) || !allowedPath(to, false)) return sendErr(403, "path not allowed");
  if (!Storage::exists(from)) return sendErr(404, "not found");
  if (Storage::exists(to)) return sendErr(409, "a file with that name already exists");
  if (!Storage::rename(from, to)) return sendErr(500, "rename failed");
  event("Renamed " + to.substring(to.lastIndexOf('/') + 1));
  sendJson("{\"ok\":true}");
}

// Files inside app folders, read-only: /app/<id>/<file> -> /apps/<id>/<file>
// Lets an app ship its own web page (tab.page=) plus any scripts it needs.
static const char* contentType(const String& p) {
  if (p.endsWith(".html") || p.endsWith(".htm")) return "text/html";
  if (p.endsWith(".js"))   return "application/javascript";
  if (p.endsWith(".css"))  return "text/css";
  if (p.endsWith(".json")) return "application/json";
  if (p.endsWith(".png"))  return "image/png";
  if (p.endsWith(".svg"))  return "image/svg+xml";
  if (p.endsWith(".txt") || p.endsWith(".md") || p.endsWith(".lua") || p.endsWith(".ini")) return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

static void hNotFound() {
  const String uri = server.uri();
  if (uri.startsWith("/app/") && uri.indexOf("..") < 0) {
    const String p = String(APPS_DIR) + uri.substring(4);          // "/app/x/y" -> "/apps/x/y"
    if (Storage::exists(p) && !Storage::isDir(p)) {
      File f = SD.open(p.c_str(), FILE_READ);
      if (f) {
        server.sendHeader("Cache-Control", "max-age=300");
        server.streamFile(f, contentType(p));
        f.close();
        return;
      }
    }
  }
  sendErr(404, "not found");
}

static void hUploadChunk() {
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      upOk = false;
      upErr = "";
      upPath = server.arg("path");
      if (!allowedPath(upPath, false)) { upErr = "path not allowed"; return; }
      if (!Storage::mounted()) { upErr = "no SD card"; return; }
      Storage::mkdirs(Storage::parentOf(upPath));
      upTmp = Storage::tempPath(upPath);              // crash-safe: see Storage::commit()
      if (SD.exists(upTmp.c_str())) SD.remove(upTmp.c_str());
      upFile = SD.open(upTmp.c_str(), FILE_WRITE);
      if (!upFile) upErr = "can't create file";
      break;

    case UPLOAD_FILE_WRITE:
      if (upFile && upFile.write(up.buf, up.currentSize) != up.currentSize) {
        upErr = "write failed (card full?)";
        upFile.close();
        SD.remove(upTmp.c_str());
      }
      break;

    case UPLOAD_FILE_END:
      if (!upFile) break;
      upFile.close();
      if (Storage::commit(upPath)) {
        upOk = true;
        received++;
        event("Got " + upPath);
      } else {
        upErr = "rename failed";
      }
      break;

    case UPLOAD_FILE_ABORTED:
      if (upFile) { upFile.close(); SD.remove(upTmp.c_str()); }
      upErr = "upload aborted";
      break;

    default:
      break;
  }
}

// ---- Firmware update: the .bin goes to the SD card, then waits for you to
// confirm on the device (the Uploader shows the dialog). Nothing is flashed here.
static bool   fwPending = false;
static String fwVersion;
static uint32_t fwSize = 0;

static void hFirmwareChunk() {
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      upOk = false;
      upErr = "";
      fwPending = false;
      if (!Storage::mounted()) { upErr = "no SD card"; return; }
      if (!Ota::available()) {
        upErr = "This device can't update over WiFi yet: set the partition scheme to "
                "\"16M Flash (3MB APP/9.9MB FATFS)\" and upload once over USB.";
        return;
      }
      upPath = UPDATE_FILE;
      upTmp = Storage::tempPath(upPath);
      if (SD.exists(upTmp.c_str())) SD.remove(upTmp.c_str());
      upFile = SD.open(upTmp.c_str(), FILE_WRITE);
      if (!upFile) upErr = "can't create the update file";
      break;
    case UPLOAD_FILE_WRITE:
      if (upFile && upFile.write(up.buf, up.currentSize) != up.currentSize) {
        upErr = "write failed (card full?)";
        upFile.close();
        SD.remove(upTmp.c_str());
      }
      break;
    case UPLOAD_FILE_END:
      if (!upFile) break;
      upFile.close();
      if (!Storage::commit(upPath)) { upErr = "couldn't save the update file"; break; }
      {
        String why;
        if (Ota::inspect(upPath, fwVersion, fwSize, why)) {
          upOk = true;
          fwPending = true;
          event("Firmware " + fwVersion + " received");
        } else {
          upErr = why;
          SD.remove(upPath.c_str());
        }
      }
      break;
    case UPLOAD_FILE_ABORTED:
      if (upFile) { upFile.close(); SD.remove(upTmp.c_str()); }
      upErr = "upload aborted";
      break;
    default:
      break;
  }
}

static void hFirmwareDone() {
  if (upOk) sendJson("{\"ok\":true,\"version\":" + jstr(fwVersion) + ",\"size\":" + String((unsigned long)fwSize) + "}");
  else      sendErr(400, upErr.length() ? upErr.c_str() : "upload failed");
}

bool updatePending(String& version, uint32_t& size) {
  if (!fwPending) return false;
  version = fwVersion;
  size = fwSize;
  return true;
}

void clearUpdate(bool removeFile) {
  fwPending = false;
  if (removeFile) Storage::remove(UPDATE_FILE);
}

static void hUploadDone() {
  if (upOk) sendJson("{\"ok\":true}");
  else      sendErr(500, upErr.length() ? upErr.c_str() : "upload failed");
}

static void hInfo() {
  String saved, pass;
  readWifi(saved, pass);
  String net;
  switch (curMode) {
    case Mode::Station:    net = "WiFi: " + netName; break;
    case Mode::Hotspot:    net = "Hotspot: " + netName; break;
    case Mode::Connecting: net = "Connecting to " + netName; break;
    case Mode::Scanning:   net = "Scanning"; break;
    default:               net = "not connected"; break;
  }
  char heap[48], psram[24];
  snprintf(heap, sizeof(heap), "%lu KB free", (unsigned long)(ESP.getFreeHeap() / 1024));
  snprintf(psram, sizeof(psram), "%lu KB", (unsigned long)(ESP.getPsramSize() / 1024));
  String j = "{";
  j += "\"firmware\":\"" FW_VERSION "\"";
  j += ",\"chip\":" + jstr(String(ESP.getChipModel()) + " @ " + String(ESP.getCpuFreqMHz()) + " MHz");
  j += ",\"heap\":" + jstr(heap);
  j += ",\"psram\":" + jstr(psram);
  j += ",\"display\":" + jstr(String(screen.backendName()) + " " + String(SCREEN_W) + "x" + String(SCREEN_H));
  j += ",\"sd\":" + jstr(Storage::cardInfo());
  j += ",\"network\":" + jstr(net);
  j += ",\"ip\":" + jstr(address() + "  (http://" MDNS_NAME ".local)");
  j += ",\"apps\":" + jstr(String((unsigned)LuaHost::readManifests().size()));
  j += ",\"saved_ssid\":" + jstr(saved);
  j += ",\"ota\":" + String(Ota::available() ? "true" : "false");
  j += ",\"ota_slot\":" + String((unsigned long)Ota::slotSize());
  sendJson(j + "}");
}

static void hWifi() {
  String ssid = server.arg("ssid");
  const String pass = server.arg("password");
  ssid.trim();
  bool ok;
  if (!ssid.length()) {
    ok = !Storage::exists(WIFI_CONFIG_FILE) || Storage::remove(WIFI_CONFIG_FILE);
    event("WiFi cleared: hotspot next time");
  } else {
    ok = Storage::writeText(WIFI_CONFIG_FILE, ssid + "\n" + pass + "\n");
    event("WiFi saved: " + ssid);
  }
  if (ok) sendJson("{\"ok\":true}");
  else    sendErr(500, "couldn't save (SD card?)");
}

// GET: the device clock. POST: epoch= (set the time) and/or tz= (POSIX timezone)
static void hTime() {
  if (server.method() == HTTP_POST) {
    bool ok = true;
    if (server.hasArg("tz")) ok = Clock::setTz(server.arg("tz")) && ok;
    if (server.hasArg("h12")) ok = Clock::set12h(server.arg("h12") == "1") && ok;
    if (server.hasArg("epoch")) {
      const uint32_t e = strtoul(server.arg("epoch").c_str(), nullptr, 10);
      ok = Clock::setEpoch(e, "browser") && ok;
      if (ok) event("Time set: " + Clock::hhmm());
    }
    if (!ok) return sendErr(400, "couldn't set that");
  }
  String j = "{\"valid\":" + String(Clock::valid() ? "true" : "false");
  j += ",\"epoch\":" + String((unsigned long)time(nullptr));
  j += ",\"local\":" + jstr(Clock::dateLine());
  j += ",\"source\":" + jstr(Clock::source());
  j += ",\"rtc\":" + jstr(Clock::rtcName() ? Clock::rtcName() : "");
  j += ",\"clock\":" + jstr(Clock::hhmm());
  j += ",\"h12\":" + String(Clock::use12h() ? "true" : "false");
  j += ",\"tz\":" + jstr(Clock::tz()) + "}";
  sendJson(j);
}

static void setupRoutes() {
  server.on("/", HTTP_GET, hIndex);
  server.on("/api/tabs", HTTP_GET, hTabs);
  server.on("/api/apps", HTTP_GET, hApps);
  server.on("/api/list", HTTP_GET, hList);
  server.on("/api/info", HTTP_GET, hInfo);
  server.on("/api/download", HTTP_GET, hDownload);
  server.on("/api/delete", HTTP_POST, hDelete);
  server.on("/api/wifi", HTTP_POST, hWifi);
  server.on("/api/upload", HTTP_POST, hUploadDone, hUploadChunk);
  server.on("/api/firmware", HTTP_POST, hFirmwareDone, hFirmwareChunk);
  server.on("/api/rename", HTTP_POST, hRename);
  server.on("/api/time", HTTP_ANY, hTime);
  server.onNotFound(hNotFound);
  routesSet = true;
}

// =====================================================================
//  Lifecycle
// =====================================================================
static void startServer() {
  if (!routesSet) setupRoutes();
  if (!serverUp) {
    server.begin();
    serverUp = true;
  }
  // The address may have changed (new network / hotspot), so restart mDNS
  if (mdnsUp) MDNS.end();
  mdnsUp = MDNS.begin(MDNS_NAME);
  if (mdnsUp) MDNS.addService("http", "tcp", 80);
  Serial.printf("[web] server up at http://%s\n", address().c_str());
}

static void dropConnection() {
  if (scanning) { WiFi.scanDelete(); scanning = false; }
  if (curMode == Mode::Hotspot) WiFi.softAPdisconnect(true);
  WiFi.disconnect(false);
}

void startScan() {
  dropConnection();
  nets.clear();
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  WiFi.scanNetworks(true);                 // async: results arrive in loop()
  scanning = true;
  curMode = Mode::Scanning;
  event("Scanning for networks");
}

void connectTo(const String& ssid, const String& password) {
  dropConnection();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  netName = ssid;
  pendingSsid = ssid;
  pendingPass = password;
  pendingSave = true;
  noticeMsg = "";
  curMode = Mode::Connecting;
  t0 = millis();
  event("Connecting to " + ssid);
}

void startHotspot() {
  dropConnection();
  WiFi.mode(WIFI_AP);
  netName = hotspotName();
  WiFi.softAP(netName.c_str(), AP_PASSWORD);
  curMode = Mode::Hotspot;
  noticeMsg = "";
  startServer();
  event("Hotspot started");
}

void start() {
  if (curMode != Mode::Off) return;
  received = 0;
  lastEv = "";
  noticeMsg = "";
  String ssid, pass;
  if (readWifi(ssid, pass)) {
    connectTo(ssid, pass);
    pendingSave = false;                   // already saved
  } else {
    startScan();
  }
}

static void finishScan(int n) {
  scanning = false;
  nets.clear();
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;          // hidden network
    const int rssi = WiFi.RSSI(i);
    const bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    auto it = std::find_if(nets.begin(), nets.end(), [&](const Network& x) { return x.ssid == ssid; });
    if (it == nets.end())      nets.push_back({ ssid, rssi, open });
    else if (rssi > it->rssi)  { it->rssi = rssi; it->open = open; }   // same name, several access points
  }
  std::sort(nets.begin(), nets.end(), [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
  WiFi.scanDelete();
  curMode = Mode::Idle;
  event(n >= 0 ? String((unsigned)nets.size()) + " networks found" : String("Scan failed"));
}

static void connectFailed(const char* why) {
  noticeMsg = "Couldn't join " + netName + " (" + why + ")";
  pendingSave = false;
  Serial.printf("[web] %s\n", noticeMsg.c_str());
  startScan();                             // back to the list so another network can be picked
}

void loop() {
  if (scanning) {
    const int n = WiFi.scanComplete();
    if (n >= 0 || n == WIFI_SCAN_FAILED) finishScan(n);
  }

  if (curMode == Mode::Connecting) {
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      curMode = Mode::Station;
      Clock::ntpStart();                       // online: set the clock (and the RTC) from the internet
      if (pendingSave) {
        Storage::writeText(WIFI_CONFIG_FILE, pendingSsid + "\n" + pendingPass + "\n");
        pendingSave = false;
      }
      startServer();
      event("Connected");
    } else if (st == WL_CONNECT_FAILED) {
      connectFailed("wrong password?");
    } else if (st == WL_NO_SSID_AVAIL && millis() - t0 > 5000) {
      connectFailed("not in range");
    } else if (millis() - t0 > WIFI_CONNECT_MS) {
      connectFailed("timed out - wrong password?");
    }
  }

  if (serverUp) server.handleClient();
}

void stop() {
  if (upFile) { upFile.close(); SD.remove(upTmp.c_str()); }
  if (serverUp) {
    server.stop();
    serverUp = false;
  }
  if (mdnsUp) { MDNS.end(); mdnsUp = false; }
  if (curMode != Mode::Off) {
    if (scanning) { WiFi.scanDelete(); scanning = false; }
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[web] WiFi off");
  }
  nets.clear();
  curMode = Mode::Off;
}

Mode mode()               { return curMode; }
const std::vector<Network>& networks() { return nets; }
String notice()           { return noticeMsg; }
String networkName()      { return netName; }
String lastEvent()        { return lastEv; }
uint32_t filesReceived()  { return received; }

String address() {
  if (curMode == Mode::Station) return WiFi.localIP().toString();
  if (curMode == Mode::Hotspot) return WiFi.softAPIP().toString();
  return "";
}

bool changed() {
  const bool c = dirtyFlag;
  dirtyFlag = false;
  return c;
}

}
