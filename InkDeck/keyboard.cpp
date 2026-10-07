#include "keyboard.h"
#include <Wire.h>
#include "config.h"

CardKB kb;

static bool probeCardKB() {
  Wire.beginTransmission(CARDKB_ADDR);
  return Wire.endTransmission() == 0;
}

bool CardKB::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);
  // The CardKB's own chip starts up slower than the ESP32: give it up to half a second
  for (int i = 0; i < 10 && !(_present = probeCardKB()); i++) delay(50);
  _lastProbe = millis();
  Serial.printf("[kb] CardKB %s at 0x%02X (SDA=%d SCL=%d)\n",
                _present ? "found" : "NOT found", CARDKB_ADDR, PIN_I2C_SDA, PIN_I2C_SCL);
  return _present;
}

uint8_t CardKB::poll() {
  service();
  if (_head == _tail) return 0;
  const uint8_t c = _q[_tail];
  _tail = (_tail + 1) % QUEUE;
  return c;
}

void CardKB::service() {
  uint32_t now = millis();
  if (now - _lastPoll < KB_POLL_MS) return;
  _lastPoll = now;

  // Not detected: re-probe every 200 ms, so hot-plugging works and a dropout is short
  if (!_present) {
    if (now - _lastProbe > 200) {
      _lastProbe = now;
      _present = probeCardKB();
      if (_present) Serial.println("[kb] CardKB connected");
    }
    return;
  }

  // A failed read is usually a glitch (e.g. a supply dip while the buzzer sounds on
  // battery): just try again next poll. Only 10 in a row (100 ms) means it's gone.
  if (Wire.requestFrom((uint8_t)CARDKB_ADDR, (uint8_t)1) != 1) {
    if (++_misses >= 10) {
      _present = false;
      _misses = 0;
      _lastProbe = now;
      Serial.println("[kb] CardKB lost");
    }
    return;
  }
  _misses = 0;

  uint8_t c = Wire.read();
  if (!c) return;
  Serial.printf("[kb] key 0x%02X\n", c);         // handy for clones with odd codes
  const uint8_t next = (_head + 1) % QUEUE;
  if (next != _tail) { _q[_head] = c; _head = next; }   // full: drop (64 keys behind is plenty)
}
