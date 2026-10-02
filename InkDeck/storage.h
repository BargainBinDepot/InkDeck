#pragma once
// =====================================================================
//  Storage — microSD on its own SPI bus (HSPI), separate from the display.
//  Folder layout created on first mount:
//    /notes   Notes app (.txt)
//    /apps    Lua apps, one folder each (app.ini + main.lua + data)
//    /system  settings (wifi.txt), logs
// =====================================================================
#include <Arduino.h>
#include <vector>
#include <memory>

#define NOTES_DIR  "/notes"
#define APPS_DIR   "/apps"
#define SYSTEM_DIR "/system"

namespace Storage {

struct Entry {
  String   name;     // file or folder name (no path)
  bool     dir;
  uint32_t size;
};

bool begin();                     // mount (safe to call again to retry)
bool mounted();

bool exists(const String& path);
bool isDir(const String& path);
uint32_t fileSize(const String& path);
bool remove(const String& path);          // single file
bool removeTree(const String& path);      // file or whole folder
bool mkdirs(const String& dirPath);       // create every missing folder in the path
bool readText(const String& path, String& out, size_t maxLen);   // stops at maxLen (check fileSize first if that matters)
bool writeText(const String& path, const String& data);   // crash-safe: see commit(); creates parent folders

// Crash-safe replace: write the new contents to tempPath(path), then call
// commit(path). The original is renamed to a backup before the new file
// moves in, so at no point is there no complete copy on the card.
String tempPath(const String& path);
bool commit(const String& path);
int  recover();                                    // at boot: finish or undo interrupted saves
std::vector<Entry> list(const String& dir);               // folders first, then A-Z

bool rename(const String& from, const String& to);   // replaces `to` if it exists
uint64_t dirSize(const String& path, uint32_t* files = nullptr, uint32_t* dirs = nullptr);   // total bytes, recursive
String parentOf(const String& path);

// Fast sequential byte reader (buffered), used by the text engine
struct ByteReader {
  virtual ~ByteReader() {}
  virtual int read() = 0;            // next byte, or -1 at end of file
  virtual uint32_t pos() = 0;        // offset of the next byte read() will return
  virtual uint32_t size() = 0;
};
std::unique_ptr<ByteReader> openReader(const String& path, uint32_t offset = 0);
String cardInfo();                // e.g. "SDHC 29.7 GB, 12 MB used"

}
