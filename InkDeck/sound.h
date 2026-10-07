#pragma once
// =====================================================================
//  Sound — a passive buzzer on PIN_BUZZER (through an NPN transistor;
//  wiring on the battery wiring page). Tones are square waves from the
//  LEDC peripheral, so playing never blocks: a beep or a short tune
//  plays in the background while the app carries on.
//
//  For apps: sys.beep(freq, ms), sys.beep({ {freq, ms}, ... }) and
//  sys.sound() (see APP_API.md). System > Settings > Sound turns it off.
// =====================================================================
#include <Arduino.h>
#include <vector>

namespace Sound {

struct Note { uint16_t freq; uint16_t ms; };   // freq 0 = a rest

void begin();                         // after Storage::begin(): reads the On/Off setting
void loop();                          // call often: moves a tune on to its next note
bool available();                     // a buzzer is configured and sound is on
bool hasBuzzer();                     // a buzzer is configured (PIN_BUZZER >= 0)
bool enabled();                       // the Settings switch
bool setEnabled(bool on);             // saved on the SD card

bool play(const std::vector<Note>& tune);   // replaces whatever is playing; false if unavailable
bool beep(uint16_t freq = 2700, uint16_t ms = 80);
void stop();

// A short tick for the arrow keys (Settings > Arrow key click). Skipped while
// an app's beep or tune is playing, so it never cuts one off.
void click();
bool clicks();
bool setClicks(bool on);

}
