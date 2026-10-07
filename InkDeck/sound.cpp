#include "sound.h"
#include "config.h"
#include "storage.h"

namespace Sound {

static bool on = true;

bool hasBuzzer()  { return PIN_BUZZER >= 0; }
bool enabled()    { return on; }
bool available()  { return hasBuzzer() && on; }

#if PIN_BUZZER >= 0

static std::vector<Note> queue;
static size_t next = 0;               // index of the note to start next
static uint32_t noteEnd = 0;          // millis() when the current note ends
static bool playing = false;

static void startNote(const Note& n) {
  ledcWriteTone(PIN_BUZZER, n.freq);  // 0 = silent (a rest)
  noteEnd = millis() + n.ms;
}

void begin() {
  String cfg;
  if (Storage::readText(SOUND_CONFIG_FILE, cfg, 64)) on = cfg.indexOf("on=0") < 0;
  ledcAttach(PIN_BUZZER, 2700, 10);
  ledcWriteTone(PIN_BUZZER, 0);       // pin low: transistor off, no current through the buzzer
  Serial.printf("[sound] buzzer on GPIO %d, sound %s\n", PIN_BUZZER, on ? "on" : "off");
}

void stop() {
  ledcWriteTone(PIN_BUZZER, 0);
  queue.clear();
  next = 0;
  playing = false;
}

bool play(const std::vector<Note>& tune) {
  stop();
  if (!on || tune.empty()) return false;
  queue = tune;
  next = 1;
  playing = true;
  startNote(queue[0]);
  return true;
}

void loop() {
  if (!playing || (int32_t)(millis() - noteEnd) < 0) return;
  if (next < queue.size()) startNote(queue[next++]);
  else stop();
}

#else   // no buzzer

void begin() {}
void loop() {}
void stop() {}
bool play(const std::vector<Note>&) { return false; }

#endif

bool beep(uint16_t freq, uint16_t ms) { return play({ { freq, ms } }); }

bool setEnabled(bool v) {
  on = v;
  if (!on) stop();
  return Storage::writeText(SOUND_CONFIG_FILE, String("on=") + (on ? "1" : "0") + "\n");
}

}
