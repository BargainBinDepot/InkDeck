#pragma once
// =====================================================================
//  Progress — a progress bar under the status bar while a long job runs
//  inside the firmware (an app's download, a book being converted).
//  An app turns it on with sys.progress(title); the jobs report what they're
//  doing and the screen is refreshed every couple of seconds (e-paper is slow).
// =====================================================================
#include <Arduino.h>

namespace Progress {

enum class Unit { Count, Bytes, Percent };   // "12 / 40", "1.2 / 3.4 MB", or just the bar

void begin(const String& title);
void end();
bool active();
// total 0 = not known (bytes so far, and a moving bar)
void update(const char* stage, uint32_t done, uint32_t total, Unit unit = Unit::Count);
void note(const String& detail);            // an extra line under the bar, e.g. "14 pictures"

}
