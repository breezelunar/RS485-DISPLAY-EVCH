#include "encoder_lib.h"

const int8_t EncoderLib::QUAD[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

void EncoderLib::begin(uint8_t pinA, uint8_t pinB, uint8_t pinSW,
                       int8_t detentDiv, uint16_t debounceMs) {
  _a = pinA; _b = pinB; _sw = pinSW;
  _detDiv = (detentDiv == 0) ? 1 : detentDiv;
  _debounce = debounceMs;

  pinMode(_a, INPUT_PULLUP);
  pinMode(_b, INPUT_PULLUP);
  pinMode(_sw, INPUT_PULLUP);

  _prevAB = (digitalRead(_a) ? 2 : 0) | (digitalRead(_b) ? 1 : 0);

  _steps = 0;

  _swStable = true;
  _swLastRaw = true;
  _swLastStable = true;
  _swT = millis();
}

// Called from pin-change ISR — only reads A/B, no millis(), no digitalRead of switch
void EncoderLib::isrUpdate() {
  uint8_t a = digitalRead(_a) ? 1 : 0;
  uint8_t b = digitalRead(_b) ? 1 : 0;
  uint8_t ab = (a << 1) | b;

  _steps += QUAD[(_prevAB << 2) | ab];
  _prevAB = ab;
}

// Called from main loop — handles button debounce only
void EncoderLib::update() {
  bool raw = digitalRead(_sw);
  if (raw != _swLastRaw) {
    _swLastRaw = raw;
    _swT = millis();
  }
  if (millis() - _swT >= _debounce) {
    _swStable = raw;
  }
}

int16_t EncoderLib::deltaDetents() {
  noInterrupts();
  int16_t steps = _steps;
  int16_t det = steps / _detDiv;
  if (det != 0) _steps -= det * _detDiv;
  interrupts();
  return det;
}

bool EncoderLib::pressed() {
  bool ev = (_swLastStable == true && _swStable == false);
  _swLastStable = _swStable;
  return ev;
}