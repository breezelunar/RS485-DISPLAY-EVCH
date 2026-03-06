#include <Arduino.h>
#include <U8g2lib.h>
#include <SoftwareSerial.h>

#include "oled_lib.h"
#include "mppt_rs485.h"
#include "encoder_lib.h"

// ---------------- Pins ----------------
const uint8_t ENC_A  = PIN_PD2;
const uint8_t ENC_B  = PIN_PD3;
const uint8_t ENC_SW = PIN_PD4;

const uint8_t RS485_TX  = PIN_PC0;
const uint8_t RS485_RX  = PIN_PC1;
const uint8_t RS485_DIR = PIN_PD5;

// ---------------- Instances ----------------
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
SoftwareSerial rs485(RS485_RX, RS485_TX);

OledUI ui(u8g2, 0x3C);
MpptRS485 mpptBus(rs485, RS485_DIR);
EncoderLib enc;

// ---------------- Encoder ISR ----------------
void encoderISR() {
  enc.isrUpdate();
}

// ---------------- Data Structures ----------------
MpptData mpptData;
MpptSettings mpptSettings;

// ---------------- Settings ----------------
uint16_t vmpVoltage    = 620;
uint8_t chargeLimit    = 100;

const uint8_t chargeLimitOptions[] = {5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
const uint8_t chargeLimitCount = 11;

// ---------------- UI State ----------------
enum UiMode : uint8_t {
  HOME_MODE,
  MORE_OPTIONS_MODE,
  INSTANTANEOUS_DATA_MODE,
  EDIT_SETTINGS_MODE,
  EDIT_SETTINGS_EDIT_MODE,
  VIEW_SETTINGS_MODE
};

UiMode mode = HOME_MODE;
uint8_t menuIndex = 0;
uint8_t chargeLimitIndex = 10;

uint32_t lastUiUpdate = 0;
uint32_t lastRequest = 0;

// ---------------- Bus State ----------------
enum BusState { BUS_IDLE, BUS_WAIT_RESPONSE, BUS_READING, BUS_SENT };
BusState busState = BUS_IDLE;
uint32_t busTime = 0;
bool busIsSettings = false;

// ---------------- Saving State ----------------
enum SaveType { SAVE_NONE, SAVE_SETTINGS };
SaveType saveType = SAVE_NONE;
bool saving = false;
bool postSaving = false;
uint32_t postSaveTime = 0;

// ---------------- Constants ----------------
const uint16_t MPPT_ADDR = 5;
const uint16_t DATA_RESPONSE_WAIT = 20;
const uint16_t SETTINGS_RESPONSE_WAIT = 35;
const uint16_t REQ_INTERVAL = 30;
const uint16_t UI_REFRESH_MS = 50;


// ---------------- Helpers ----------------
uint16_t getWaitTime(bool isSettings) {
  return isSettings ? SETTINGS_RESPONSE_WAIT : DATA_RESPONSE_WAIT;
}

void initiateSaveSettings() {
  saving = true;
  saveType = SAVE_SETTINGS;
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);

  ui.begin();
  enc.begin(ENC_A, ENC_B, ENC_SW, 4, 20);
  mpptBus.begin(9600);

  // Attach interrupts on encoder A and B pins (PD2 = INT0, PD3 = INT1)
  attachInterrupt(digitalPinToInterrupt(ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encoderISR, CHANGE);

  mpptData.valid = false;
  mpptData.err = 0;
  mpptSettings.valid = false;
  mpptSettings.err = 0;

  ui.drawHomePage(mpptData);
}

// ---------------- Loop ----------------
void loop() {
  enc.update();    // Button debounce only — rotation is handled by ISR
  int16_t det = enc.deltaDetents();
  bool pressed = enc.pressed();

  if (det > 0) {
    Serial.println("cw");
  } else if (det < 0) {
    Serial.println("acw");
  }
  if (pressed) {
    Serial.println("clk");
  }

  uint32_t now = millis();

  // Bus state machine
  if (busState == BUS_WAIT_RESPONSE && now - busTime >= getWaitTime(busIsSettings)) {
    mpptBus.startRead(MPPT_ADDR, busIsSettings);
    busState = BUS_READING;
  }

  if (busState == BUS_READING) {
    if (mpptBus.updateRead()) {
      busState = BUS_IDLE;
    }
  }

  if (busState == BUS_SENT && now - busTime >= 150) {
    busState = BUS_IDLE;
  }

  // Saving state machine
  if (saving && busState == BUS_IDLE) {
    if (saveType == SAVE_SETTINGS) {
      mpptBus.sendSettings(MPPT_ADDR, vmpVoltage, chargeLimit);
    }
    busState = BUS_SENT;
    busTime = millis();
    saving = false;
    postSaving = true;
    postSaveTime = millis();
  }

  if (postSaving && now - postSaveTime >= 2000) {
    postSaving = false;
    mode = MORE_OPTIONS_MODE;
  }

  // Polling for HOME mode - command 9
  if (mode == HOME_MODE && 
      busState == BUS_IDLE && now - lastRequest >= REQ_INTERVAL) {
    lastRequest = now;
    busIsSettings = false;
    mpptBus.request(MPPT_ADDR, 9);
    busState = BUS_WAIT_RESPONSE;
    busTime = millis();
  }

  // Polling for INSTANTANEOUS_DATA mode - command 3
  if (mode == INSTANTANEOUS_DATA_MODE && 
      busState == BUS_IDLE && now - lastRequest >= REQ_INTERVAL) {
    lastRequest = now;
    busIsSettings = false;
    mpptBus.request(MPPT_ADDR, 3);
    busState = BUS_WAIT_RESPONSE;
    busTime = millis();
  }

  // Polling for VIEW_SETTINGS mode - command 7
  if (mode == VIEW_SETTINGS_MODE && 
      busState == BUS_IDLE && now - lastRequest >= REQ_INTERVAL) {
    lastRequest = now;
    busIsSettings = true;
    mpptBus.request(MPPT_ADDR, 7);
    busState = BUS_WAIT_RESPONSE;
    busTime = millis();
  }

  // Input handling
  if (det != 0 || pressed) {
    if (saving || postSaving) return;

    lastUiUpdate = now;

    if(mode == HOME_MODE) {
      if(pressed) {
        mode = MORE_OPTIONS_MODE;
        menuIndex = 0;
      }
    }
    else if(mode == MORE_OPTIONS_MODE) {
      if(det) {
        int ni = (int)menuIndex + det;
        menuIndex = constrain(ni, 0, 3);
      }
      if(pressed) {
        if(menuIndex == 0) { mode = INSTANTANEOUS_DATA_MODE; lastRequest = 0; }
        else if(menuIndex == 1) { mode = EDIT_SETTINGS_MODE; menuIndex = 0; }
        else if(menuIndex == 2) { mode = VIEW_SETTINGS_MODE; lastRequest = 0; }
        else { mode = HOME_MODE; }
      }
    }
    else if(mode == INSTANTANEOUS_DATA_MODE) {
      if(pressed) {
        mode = MORE_OPTIONS_MODE;
      }
    }
    else if(mode == EDIT_SETTINGS_MODE || mode == EDIT_SETTINGS_EDIT_MODE) {
      if(det) {
        if(mode == EDIT_SETTINGS_MODE) {
          int ni = (int)menuIndex + det;
          menuIndex = constrain(ni, 0, 3);
        } else {
          if(menuIndex == 0) {
            int nv = (int)vmpVoltage + det*10;
            vmpVoltage = constrain(nv, 550, 700);
          }
          else if(menuIndex == 1) {
            int ni = (int)chargeLimitIndex + det;
            chargeLimitIndex = constrain(ni, 0, chargeLimitCount - 1);
            chargeLimit = chargeLimitOptions[chargeLimitIndex];
          }
        }
      }
      if(pressed) {
        if(mode == EDIT_SETTINGS_MODE) {
          if(menuIndex == 2) {
            initiateSaveSettings();
          }
          else if(menuIndex == 3) {
            mode = MORE_OPTIONS_MODE;
          }
          else {
            mode = EDIT_SETTINGS_EDIT_MODE;
          }
        } else {
          mode = EDIT_SETTINGS_MODE;
        }
      }
    }
    else if(mode == VIEW_SETTINGS_MODE) {
      if(pressed) {
        mode = MORE_OPTIONS_MODE;
      }
    }
  }

  // UI refresh
  if(now - lastUiUpdate >= UI_REFRESH_MS) {
    lastUiUpdate = now;

    if (saving || postSaving) {
      ui.drawSavingMessage();
    } else {
      if(mode == HOME_MODE) {
        ui.drawHomePage(mpptData);
      }
      else if(mode == MORE_OPTIONS_MODE) {
        ui.drawMoreOptions(menuIndex);
      }
      else if(mode == INSTANTANEOUS_DATA_MODE) {
        ui.drawInstantaneousMpptCard(mpptData);
      }
      else if(mode == EDIT_SETTINGS_MODE || mode == EDIT_SETTINGS_EDIT_MODE) {
        ui.drawEditSettingsPage(menuIndex, mode == EDIT_SETTINGS_EDIT_MODE);
      }
      else if(mode == VIEW_SETTINGS_MODE) {
        ui.drawMpptSettingsPage(MPPT_ADDR, mpptSettings);
      }
    }
  }
}