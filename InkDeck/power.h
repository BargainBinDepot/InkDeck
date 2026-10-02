#pragma once
// =====================================================================
//  Power — sleep after a period without key presses.
//
//  Sleep is ESP32 light sleep: RAM, apps and unsaved text all stay put,
//  and the screen keeps showing what it was showing (e-paper needs no
//  power for that). The title-bar clock is replaced by "zZ" first, so a
//  frozen time is never mistaken for the real one. The keyboard is
//  checked a few times a second; any key wakes the device and is
//  swallowed (so waking never types a stray letter).
//
//  No sleeping while the Uploader is running, while the clock is syncing,
//  or while an app has asked to stay awake (sys.stayawake).
// =====================================================================
#include <Arduino.h>

namespace Power {

void begin();                   // loads the saved timeout
void loop();                    // call every loop; may block while asleep
void noteInput();               // a key was pressed: restart the countdown

bool sleeping();                // true while going to sleep (the title bar shows "zZ")
void keepAwake(bool on);        // an app asks to stay awake

uint32_t timeoutSec();          // 0 = never
bool     setTimeoutSec(uint32_t s);

}
