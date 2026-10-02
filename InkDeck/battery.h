#pragma once
// =====================================================================
//  Battery — LiPo level from a voltage divider on PIN_BATT_ADC.
//
//    BAT+ ──[100k]──┬──[100k]── GND        (BATT_DIVIDER 2.0)
//                   ├── PIN_BATT_ADC
//                   └── 100nF ── GND        (feeds the ADC; the resistors can't)
//
//  Optional: USB 5V through another 100k/100k divider to PIN_USB_SENSE
//  shows charging (the TP4056's input is the USB-C 5V).
//
//  Shown as a 4-bar meter: 4 = full .. 1 = a quarter, 0 = empty (about 10%).
//  Readings are taken while idle (not during WiFi), averaged and smoothed,
//  and the bars only go down while on battery, so the meter doesn't flicker.
//  At BATT_SHUTDOWN_V the device shows "Battery empty" and deep-sleeps
//  before the 3.3V rail sags and SD writes go wrong.
// =====================================================================
#include <Arduino.h>

namespace Battery {

void begin();             // first thing in setup(): may go straight back to deep sleep if still empty
void loop();              // call every loop
bool present();           // monitoring on and a plausible battery voltage seen
int  level();             // 0..4 bars (0 = empty / about 10%), -1 if not present
float volts();            // smoothed battery voltage, 0 if not present
bool charging();          // USB power on PIN_USB_SENSE (false if that isn't wired)
bool critical();          // at or below BATT_SHUTDOWN_V: time to shut down
void measureNow();        // take a reading now (used by the sleep loop)
String label();           // e.g. "3.87 V, 3/4" for About / the web page

}
