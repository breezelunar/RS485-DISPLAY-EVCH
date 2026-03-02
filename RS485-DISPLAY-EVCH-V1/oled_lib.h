#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "mppt_rs485.h"

class OledUI {
public:
  OledUI(U8G2& display, uint8_t i2cAddr = 0x3C);

  bool begin();

  void drawHomePage(const MpptData& d);
  void drawMoreOptions(uint8_t selected);
  void drawInstantaneousMpptCard(const MpptData& d);
  void drawEditSettingsPage(uint8_t selected, bool editing);
  void drawMpptSettingsPage(uint8_t mpptId, const MpptSettings& d);
  void drawSavingMessage();

private:
  U8G2& _u8;
  uint8_t _addr;

  static bool i2cPresent(uint8_t addr);
};