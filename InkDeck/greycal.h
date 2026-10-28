#pragma once
// =====================================================================
//  Grey calibration (System > Settings > Grey calibration)
//  A test picture for the 4-grey e-paper waveforms: the four shades, a ramp
//  in 4 greys and in black and white, and a shaded ball dithered in each of
//  the eReader converter's picture styles (Natural, Classic, Exact light),
//  with the same maths as the converter. The timings beside it are changed
//  live (Screen::setGreyTiming) and the picture is redrawn with them.
// =====================================================================
#include <Arduino.h>

namespace GreyCal {

void draw(int sel);              // test picture + the settings list, row `sel` selected
int  rows();                     // number of rows in the settings list
void change(int sel, int dir);   // Left (-1) / Right (+1) on a row; Enter on "Reset"
void activate(int sel);          // Enter on a row
const char* hint();

}
