#pragma once
#include <Arduino.h>

class EncoderLib {
public:
  EncoderLib() = default;

  void begin(uint8_t pinA, uint8_t pinB, uint8_t pinSW,
             int8_t detentDiv = 4,
             uint16_t debounceMs = 30);

  void update();
  int16_t deltaDetents();
  bool pressed();

private:
  uint8_t _a=0, _b=0, _sw=0;
  int8_t _detDiv=4;
  uint16_t _debounce=30;

  int16_t _steps=0;
  uint8_t _prevAB=0;

  bool _swStable=true, _swLastRaw=true;
  bool _swLastStable=true;
  uint32_t _swT=0;

  static const int8_t QUAD[16];
};