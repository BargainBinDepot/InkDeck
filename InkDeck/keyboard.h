#pragma once
// =====================================================================
//  CardKB (and clones) — I2C keyboard at 0x5F.
//  Returns one already-decoded byte per keypress, 0 when idle.
// =====================================================================
#include <Arduino.h>

enum KeyCode : uint8_t {
  K_NONE  = 0x00,
  K_BKSP  = 0x08,
  K_TAB   = 0x09,
  K_ENTER = 0x0D,
  K_ESC   = 0x1B,
  K_LEFT  = 0xB4,
  K_UP    = 0xB5,
  K_DOWN  = 0xB6,
  K_RIGHT = 0xB7,
  K_DEL   = 0x7F,     // Shift+Bksp: delete forward
  K_FN_ENTER = 163,   // Fn+Enter
};

inline bool isPrintableKey(uint8_t k) { return k >= 0x20 && k < 0x7F; }

// Fn combos (tap Fn, then a key) send their own codes, 128-175.
// Returns the key's name ("c", "1", "enter", "left"...), or nullptr if k isn't an Fn combo.
// Table from the CardKB v1.1 firmware (M5Stack CardKeyBoard.ino, KeyMap fn column).
inline const char* fnKeyName(uint8_t k) {
  static const char* const names[48] = {
    "esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "del", "tab",   // 128-140
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", nullptr,               // 141-151
    "left", "up", "a", "s", "d", "f", "g", "h", "j", "k", "l", "enter",      // 152-163
    "down", "right", "z", "x", "c", "v", "b", "n", "m", ",", ".", "space",   // 164-175
  };
  return (k >= 128 && k <= 175) ? names[k - 128] : nullptr;
}

class CardKB {
public:
  bool begin();                    // starts Wire and probes for the keyboard
  uint8_t poll();                  // call every loop; returns key code or 0
  bool present() const { return _present; }

private:
  bool _present = false;
  uint32_t _lastPoll = 0;
  uint32_t _lastProbe = 0;
};

extern CardKB kb;
