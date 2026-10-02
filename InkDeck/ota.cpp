#include "ota.h"
#include "config.h"
#include "storage.h"
#include <SD.h>
#include <Update.h>
#include "esp_ota_ops.h"

namespace Ota {

static const char* MARKER = "INKDECK-FW:";     // every InkDeck build carries "INKDECK-FW:<version>"

bool available() { return esp_ota_get_next_update_partition(nullptr) != nullptr; }

uint32_t slotSize() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? p->size : 0;
}

const char* runningSlot() {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p ? p->label : "?";
}

bool inspect(const String& path, String& version, uint32_t& size, String& why) {
  version = "";
  size = Storage::fileSize(path);
  auto r = Storage::openReader(path);
  if (!r || size < 1024) { why = "That file is empty or too small to be firmware."; return false; }

  // ESP32 app image header: magic 0xE9 ... chip id (2 bytes) at offset 12
  uint8_t h[24];
  for (int i = 0; i < 24; i++) { const int c = r->read(); if (c < 0) { why = "File too short."; return false; } h[i] = c; }
  if (h[0] != 0xE9) { why = "That isn't an ESP32 firmware image."; return false; }
  const uint16_t chip = h[12] | (h[13] << 8);
  if (chip != 9) { why = "That firmware is for a different chip (not an ESP32-S3)."; return false; }

  // The "merged" export has the bootloader first and a partition table at 0x8000
  if (size > 0x8002) {
    auto m = Storage::openReader(path, 0x8000);
    if (m) {
      const int a = m->read(), b = m->read();
      if (a == 0xAA && b == 0x50) { why = "That's the merged .bin. Use InkDeck.ino.bin instead."; return false; }
    }
  }

  // Look for the InkDeck tag to make sure it's our firmware, and read its version.
  // The bare tag text also appears on its own (it's this search pattern, MARKER),
  // so a match with no version after it is skipped and the search goes on.
  auto s = Storage::openReader(path);
  const size_t mlen = strlen(MARKER);
  size_t matched = 0;
  int c;
  while (!version.length() && (c = s->read()) >= 0) {
    if (c == MARKER[matched]) {
      if (++matched == mlen) {
        while ((c = s->read()) > 0 && c < 127 && version.length() < 24) version += (char)c;
        matched = (c == MARKER[0]) ? 1 : 0;
      }
    } else {
      matched = (c == MARKER[0]) ? 1 : 0;
    }
  }
  if (!version.length()) { why = "That isn't InkDeck firmware (no InkDeck tag inside)."; return false; }

  if (!available()) {
    why = "This device can't update over WiFi yet: set the partition scheme to "
          "\"16M Flash (3MB APP/9.9MB FATFS)\" and upload once over USB.";
    return false;
  }
  if (size > slotSize()) { why = "Too big for the firmware slot."; return false; }
  return true;
}

bool install(const String& path, void (*progress)(int pct), String& why) {
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) { why = "Can't open the update file."; return false; }
  const size_t total = f.size();
  if (!Update.begin(total, U_FLASH)) {
    why = String("Couldn't start: ") + Update.errorString();
    f.close();
    return false;
  }
  static uint8_t buf[4096];
  size_t done = 0;
  int lastPct = -1;
  while (done < total) {
    const size_t n = f.read(buf, sizeof(buf));
    if (n == 0) break;
    if (Update.write(buf, n) != n) break;
    done += n;
    const int pct = done * 100 / total;
    if (progress && pct / 5 != lastPct / 5) { lastPct = pct; progress(pct); }
  }
  f.close();
  if (done != total) {
    why = String("Write failed: ") + Update.errorString();
    Update.abort();
    return false;
  }
  // end(true) verifies the image and only then makes it the one to boot
  if (!Update.end(true)) {
    why = String("The new firmware didn't verify: ") + Update.errorString();
    return false;
  }
  Serial.printf("[ota] installed %u bytes into the spare slot\n", (unsigned)total);
  return true;
}

bool canRollBack() { return Update.canRollBack(); }
bool rollBack()    { return Update.rollBack(); }

void markGood() {
  const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
  Serial.printf("[ota] firmware %s marked good (%s)\n", runningSlot(), e == ESP_OK ? "ok" : "not needed");
}

}
