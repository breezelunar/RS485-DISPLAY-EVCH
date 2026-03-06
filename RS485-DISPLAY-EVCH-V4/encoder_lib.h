#pragma once
#include <Arduino.h>

class EncoderLib {
public:
  EncoderLib() = default;

  void begin(uint8_t pinA, uint8_t pinB, uint8_t pinSW,
             int8_t detentDiv = 4,
             uint16_t debounceMs = 20);

  void update();          // Still call this for button debounce
  int16_t deltaDetents();
  bool pressed();

  // Call from your ISR — samples A/B pins and updates step count
  void isrUpdate();

  // Expose pins so the sketch can attach the correct ISR
  uint8_t pinA() const { return _a; }
  uint8_t pinB() const { return _b; }

private:
  uint8_t _a=0, _b=0, _sw=0;
  int8_t _detDiv=4;
  uint16_t _debounce=20;

  volatile int16_t _steps=0;   // volatile: written from ISR
  volatile uint8_t _prevAB=0;  // volatile: written from ISR

  bool _swStable=true, _swLastRaw=true;
  bool _swLastStable=true;
  uint32_t _swT=0;

  static const int8_t QUAD[16];
};