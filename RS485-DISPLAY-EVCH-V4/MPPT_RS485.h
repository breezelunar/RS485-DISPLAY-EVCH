#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

struct MpptData {
  uint16_t vb = 0, vp = 0, ib = 0;
  uint16_t vset = 0;
  uint8_t  temp_fet = 0;
  uint8_t  temp_coil = 0;
  uint8_t  duty_cycle = 0;
  uint8_t  inst_duty_cycle = 0;
  uint8_t  pwm_min = 0;
  uint8_t  mppt_status = 0;
  uint8_t  heartbeat = 0;
  bool     valid = false;
  uint8_t  err = 0;
  uint32_t updated = 0;
};

struct MpptSettings {
  uint16_t vmp_voltage = 0;
  uint8_t  charge_limit = 0;
  bool     valid = false;
  uint8_t  err = 0;
  uint32_t updated = 0;
};

extern MpptData mpptData;
extern MpptSettings mpptSettings;

class MpptRS485 {
public:
  MpptRS485(SoftwareSerial& port,
            uint8_t dirPin,
            uint16_t srcAddr = 65000);

  void begin(uint32_t baud);
  void request(uint16_t mpptAddr, uint8_t command);
  bool sendSettings(uint16_t mpptAddr,
                    uint16_t vmpVoltage,
                    uint8_t chargeLimitPercent);

  void startRead(uint16_t expectedId, bool isSettings);
  bool updateRead();

private:
  SoftwareSerial& _s;
  uint8_t  _dir;
  uint16_t _src;

  bool reading = false;
  uint32_t readT0 = 0;
  uint32_t readLastByte = 0;
  uint8_t readBuf[80];
  int readN = 0;
  uint16_t readExpectedId = 0;
  bool readIsSettings = false;
  uint8_t readCommand = 0;

  inline void txMode();
  inline void rxMode();
  static inline uint16_t u16be(uint8_t hi, uint8_t lo);
  static uint16_t sum16(const uint8_t* b, int n);
  bool processRead();
  void staleZero(MpptData& out);
};