#pragma once
// =====================================================================
//  OTA — firmware updates over WiFi.
//
//  The Uploader's web page uploads the new firmware (.bin) to the SD card.
//  inspect() checks it is an ESP32-S3 InkDeck app image (not the "merged"
//  file), the device asks you to confirm, then install() writes it into the
//  spare firmware slot and switches over only once it's complete and verified.
//  The previous firmware stays in the other slot: rollBack() goes back to it.
//
//  Needs a partition scheme with two app slots, e.g.
//  "16M Flash (3MB APP/9.9MB FATFS)". "Huge APP" has only one.
// =====================================================================
#include <Arduino.h>

#define UPDATE_FILE "/system/update.bin"

namespace Ota {

bool     available();              // is there a spare slot to update into?
uint32_t slotSize();               // its size in bytes (0 if none)
const char* runningSlot();         // "app0" / "app1"

// Check an uploaded firmware file. On success fills version and size;
// otherwise why says what's wrong with it.
bool inspect(const String& path, String& version, uint32_t& size, String& why);

// Write it into the spare slot. progress(0..100) is called along the way.
// Returns true when the new firmware is ready; the caller restarts.
bool install(const String& path, void (*progress)(int pct), String& why);

bool canRollBack();                // does the other slot hold a firmware we can go back to?
bool rollBack();                   // switch to it (the caller restarts)

void markGood();                   // the running firmware started properly: keep it

}
