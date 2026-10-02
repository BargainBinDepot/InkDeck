#include "storage.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>
#include <algorithm>

namespace Storage {

static SPIClass sdSPI(HSPI);      // second SPI bus; the display keeps FSPI
static bool spiStarted = false;
static bool isMounted = false;

bool mounted() { return isMounted; }

bool begin() {
  if (isMounted) return true;

  if (!spiStarted) {
    sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    spiStarted = true;
  } else {
    SD.end();                     // clean up a previous failed attempt before retrying
  }

  Serial.println("[sd] mounting card...");
  if (!SD.begin(PIN_SD_CS, sdSPI, SD_SPI_HZ)) {
    Serial.println("[sd] mount failed (no card, wiring, or not FAT32)");
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    Serial.println("[sd] no card");
    return false;
  }

  isMounted = true;
  recover();                                            // finish or undo any interrupted saves
  const char* dirs[] = { NOTES_DIR, APPS_DIR, SYSTEM_DIR };
  for (const char* d : dirs) {
    if (!SD.exists(d)) {
      SD.mkdir(d);
      Serial.printf("[sd] created %s\n", d);
    }
  }
  Serial.printf("[sd] mounted: %s\n", cardInfo().c_str());
  return true;
}

bool exists(const String& path) {
  return isMounted && SD.exists(path.c_str());
}

bool isDir(const String& path) {
  if (!isMounted) return false;
  File f = SD.open(path.c_str());
  bool d = f && f.isDirectory();
  if (f) f.close();
  return d;
}

uint32_t fileSize(const String& path) {
  if (!isMounted) return 0;
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return 0;
  uint32_t s = f.size();
  f.close();
  return s;
}

String parentOf(const String& path) {
  int slash = path.lastIndexOf('/');
  return slash <= 0 ? String("/") : path.substring(0, slash);
}

bool remove(const String& path) {
  if (!isMounted) return false;
  bool ok = SD.remove(path.c_str());
  Serial.printf("[sd] remove %s: %s\n", path.c_str(), ok ? "ok" : "FAILED");
  return ok;
}

// Every entry in a folder, unfiltered and unsorted (snapshot, so it's safe to delete after)
static std::vector<Entry> rawList(const String& dir) {
  std::vector<Entry> out;
  File root = SD.open(dir.c_str());
  if (!root || !root.isDirectory()) return out;
  File f = root.openNextFile();
  while (f) {
    String name = f.name();
    int slash = name.lastIndexOf('/');           // some core versions return a full path
    if (slash >= 0) name = name.substring(slash + 1);
    out.push_back({ name, f.isDirectory(), (uint32_t)f.size() });
    f.close();
    f = root.openNextFile();
  }
  root.close();
  return out;
}

// Names and folder flags only, straight from the directory listing. rawList()
// opens every entry to get its size, and each open searches the folder from the
// top, so it slows down with the square of the file count (minutes on a book's
// folder of pictures). This stays linear. Also a snapshot.
static std::vector<Entry> quickList(const String& dir) {
  std::vector<Entry> out;
  File root = SD.open(dir.c_str());
  if (!root || !root.isDirectory()) return out;
  bool isDir = false;
  for (String name = root.getNextFileName(&isDir); name.length(); name = root.getNextFileName(&isDir)) {
    int slash = name.lastIndexOf('/');           // comes back as a full path
    if (slash >= 0) name = name.substring(slash + 1);
    out.push_back({ name, isDir, 0 });
  }
  root.close();
  return out;
}

// Sizes where that's quick, SIZE_UNKNOWN for the files of a very big folder
static std::vector<Entry> sizedList(const String& dir) {
  std::vector<Entry> q = quickList(dir);
  if (q.size() <= LIST_SIZES_MAX) return rawList(dir);
  for (auto& e : q) if (!e.dir) e.size = SIZE_UNKNOWN;
  return q;
}

uint64_t dirSize(const String& path, uint32_t* files, uint32_t* dirs, bool* partial) {
  if (!isMounted) return 0;
  if (!isDir(path)) { if (files) (*files)++; return fileSize(path); }
  uint64_t total = 0;
  for (const auto& e : sizedList(path)) {
    if (e.dir) { if (dirs) (*dirs)++; total += dirSize(path + "/" + e.name, files, dirs, partial); continue; }
    if (files) (*files)++;
    if (e.size == SIZE_UNKNOWN) { if (partial) *partial = true; }
    else total += e.size;
  }
  return total;
}

bool removeTree(const String& path) {
  if (!isMounted || path == "/") return false;
  if (!isDir(path)) return remove(path);
  for (const auto& e : quickList(path)) {
    const String child = path + "/" + e.name;
    if (e.dir) removeTree(child); else SD.remove(child.c_str());
  }
  bool ok = SD.rmdir(path.c_str());
  Serial.printf("[sd] rmdir %s: %s\n", path.c_str(), ok ? "ok" : "FAILED");
  return ok;
}

bool rename(const String& from, const String& to) {
  if (!isMounted || !SD.exists(from.c_str())) return false;
  if (from == to) return true;
  mkdirs(parentOf(to));
  bool ok;
  if (from.equalsIgnoreCase(to)) {
    // Only the capitals change. FAT names aren't case-sensitive, so `to` "exists":
    // it's this same file, and must not be deleted. Go through a temporary name.
    const String tmp = from + ".ren~";
    ok = SD.rename(from.c_str(), tmp.c_str());
    if (ok && !(ok = SD.rename(tmp.c_str(), to.c_str()))) SD.rename(tmp.c_str(), from.c_str());
  } else {
    if (SD.exists(to.c_str())) SD.remove(to.c_str());
    ok = SD.rename(from.c_str(), to.c_str());
  }
  Serial.printf("[sd] rename %s -> %s: %s\n", from.c_str(), to.c_str(), ok ? "ok" : "FAILED");
  return ok;
}

// ---- Buffered reader: SD reads in 512-byte blocks instead of byte by byte
class SdReader : public ByteReader {
public:
  File f;
  uint8_t buf[512];
  int len = 0, idx = 0;
  uint32_t base = 0, total = 0;

  int read() override {
    if (idx >= len) {
      base += len;
      len = f.read(buf, sizeof(buf));
      idx = 0;
      if (len <= 0) { len = 0; return -1; }
    }
    return buf[idx++];
  }
  uint32_t pos() override  { return base + idx; }
  uint32_t size() override { return total; }
  ~SdReader() override     { if (f) f.close(); }
};

std::unique_ptr<ByteReader> openReader(const String& path, uint32_t offset) {
  if (!isMounted) return nullptr;
  std::unique_ptr<SdReader> r(new SdReader());
  r->f = SD.open(path.c_str(), FILE_READ);
  if (!r->f || r->f.isDirectory()) return nullptr;
  r->total = r->f.size();
  if (offset > r->total) offset = r->total;
  r->f.seek(offset);
  r->base = offset;
  return std::unique_ptr<ByteReader>(r.release());
}

bool mkdirs(const String& dirPath) {
  if (!isMounted) return false;
  if (dirPath.length() == 0 || dirPath == "/") return true;
  int from = 1;
  while (true) {
    int slash = dirPath.indexOf('/', from);
    String part = slash < 0 ? dirPath : dirPath.substring(0, slash);
    if (!SD.exists(part.c_str()) && !SD.mkdir(part.c_str())) {
      Serial.printf("[sd] mkdir %s FAILED\n", part.c_str());
      return false;
    }
    if (slash < 0) return true;
    from = slash + 1;
  }
}

bool readText(const String& path, String& out, size_t maxLen) {
  out = "";
  if (!isMounted) return false;
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) { Serial.printf("[sd] can't open %s\n", path.c_str()); return false; }
  size_t sz = f.size();
  out.reserve(sz < maxLen ? sz : maxLen);
  char buf[256];
  while (f.available() && out.length() < maxLen) {
    size_t want = maxLen - out.length();
    if (want > sizeof(buf)) want = sizeof(buf);
    int n = f.read((uint8_t*)buf, want);
    if (n <= 0) break;
    for (int i = 0; i < n; i++) out += buf[i];
  }
  f.close();
  return true;
}

// ---- Crash-safe saving ------------------------------------------------------
//   1. write   path.tmp~   (the new contents), close, check the length
//   2. rename  path -> path.bak~   (the old contents, still complete)
//   3. rename  path.tmp~ -> path
//   4. delete  path.bak~
// If power is lost at any point, recover() at the next boot finds a complete
// copy: a finished .tmp~ (the new version) or the .bak~ (the old one).
static const char* TMP_SUFFIX = ".tmp~";
static const char* BAK_SUFFIX = ".bak~";

String tempPath(const String& path) { return path + TMP_SUFFIX; }

bool commit(const String& path) {
  if (!isMounted) return false;
  const String tmp = tempPath(path), bak = path + BAK_SUFFIX;
  if (SD.exists(bak.c_str())) SD.remove(bak.c_str());
  const bool had = SD.exists(path.c_str());
  if (had && !SD.rename(path.c_str(), bak.c_str())) {
    Serial.printf("[sd] commit %s: couldn't move the old file aside\n", path.c_str());
    return false;
  }
  if (!SD.rename(tmp.c_str(), path.c_str())) {
    if (had) SD.rename(bak.c_str(), path.c_str());         // put the original back
    Serial.printf("[sd] commit %s: rename FAILED\n", path.c_str());
    return false;
  }
  if (had) SD.remove(bak.c_str());
  return true;
}

bool writeText(const String& path, const String& data) {
  if (!isMounted) return false;
  mkdirs(parentOf(path));
  const String tmp = tempPath(path);

  File f = SD.open(tmp.c_str(), FILE_WRITE);
  if (!f) { Serial.printf("[sd] can't create %s\n", tmp.c_str()); return false; }
  size_t written = f.print(data);
  f.close();

  if (written != data.length()) {
    SD.remove(tmp.c_str());
    Serial.printf("[sd] short write on %s (%u of %u)\n", path.c_str(),
                  (unsigned)written, (unsigned)data.length());
    return false;
  }
  const bool ok = commit(path);
  Serial.printf("[sd] saved %s (%u bytes): %s\n", path.c_str(), (unsigned)written, ok ? "ok" : "FAILED");
  return ok;
}

// Walk the card and repair interrupted saves. Returns how many it fixed.
static int recoverDir(const String& dir) {
  int fixed = 0;
  for (const auto& e : quickList(dir)) {
    const String p = (dir == "/" ? String("") : dir) + "/" + e.name;
    if (e.dir) { fixed += recoverDir(p); continue; }
    auto baseOf = [&](const char* suffix) { return p.substring(0, p.length() - strlen(suffix)); };

    if (e.name.endsWith(TMP_SUFFIX)) {
      const String base = baseOf(TMP_SUFFIX);
      if (!SD.exists(base.c_str())) {
        // The original had already been moved aside, so this new copy was complete: finish the save
        SD.rename(p.c_str(), base.c_str());
        const String bak = base + BAK_SUFFIX;
        if (SD.exists(bak.c_str())) SD.remove(bak.c_str());
        Serial.printf("[sd] recovered %s (finished an interrupted save)\n", base.c_str());
      } else {
        SD.remove(p.c_str());                             // an unfinished write: the original is intact
        Serial.printf("[sd] removed an unfinished save of %s\n", base.c_str());
      }
      fixed++;
    } else if (e.name.endsWith(BAK_SUFFIX)) {
      const String base = baseOf(BAK_SUFFIX);
      if (!SD.exists(base.c_str()) && !SD.exists(tempPath(base).c_str())) {
        SD.rename(p.c_str(), base.c_str());               // new copy never arrived: restore the original
        Serial.printf("[sd] restored %s from its backup\n", base.c_str());
      } else if (SD.exists(base.c_str())) {
        SD.remove(p.c_str());                             // save completed; only the cleanup was missed
      }
      fixed++;
    } else if (e.name.endsWith(".tmp")) {
      // Left by older firmware (which deleted the original first). Only ever restore; never delete.
      const String base = p.substring(0, p.length() - 4);
      if (!SD.exists(base.c_str())) {
        SD.rename(p.c_str(), base.c_str());
        Serial.printf("[sd] recovered %s (from an older firmware's save)\n", base.c_str());
        fixed++;
      }
    }
  }
  return fixed;
}

int recover() {
  if (!isMounted) return 0;
  const uint32_t t = millis();
  const int n = recoverDir("/");
  Serial.printf("[sd] save check: %d repaired, %lu ms\n", n, (unsigned long)(millis() - t));
  return n;
}

std::vector<Entry> list(const String& dir) {
  std::vector<Entry> out;
  if (!isMounted) return out;
  for (const auto& e : sizedList(dir))
    if (!e.name.startsWith(".") && !e.name.endsWith(".tmp") && !e.name.endsWith(".tmp~") && !e.name.endsWith(".bak~"))
      out.push_back(e);
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
    if (a.dir != b.dir) return a.dir;
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  return out;
}

String cardInfo() {
  if (!isMounted) return "not mounted";
  const char* type = "SD";
  switch (SD.cardType()) {
    case CARD_MMC:  type = "MMC";  break;
    case CARD_SD:   type = "SDSC"; break;
    case CARD_SDHC: type = "SDHC"; break;
    default: break;
  }
  char buf[48];
  snprintf(buf, sizeof(buf), "%s %.1f GB, %lu MB used", type,
           SD.cardSize() / (1024.0 * 1024.0 * 1024.0),
           (unsigned long)(SD.usedBytes() / (1024ULL * 1024ULL)));
  return String(buf);
}

}
