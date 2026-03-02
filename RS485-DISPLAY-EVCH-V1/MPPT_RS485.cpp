#include "MPPT_RS485.h"
#include <string.h>

MpptRS485::MpptRS485(SoftwareSerial& port,
                     uint8_t dirPin,
                     uint16_t srcAddr)
: _s(port), _dir(dirPin), _src(srcAddr) {}

void MpptRS485::begin(uint32_t baud){
  pinMode(_dir, OUTPUT);
  rxMode();
  _s.begin(baud);
}

inline void MpptRS485::txMode(){ digitalWrite(_dir, HIGH); }
inline void MpptRS485::rxMode(){ digitalWrite(_dir, LOW); }

inline uint16_t MpptRS485::u16be(uint8_t hi, uint8_t lo){
  return (uint16_t(hi) << 8) | lo;
}

uint16_t MpptRS485::sum16(const uint8_t* b, int n){
  uint32_t s = 0;
  for(int i=0;i<n;i++) s += b[i];
  return (uint16_t)(s & 0xFFFF);
}

void MpptRS485::staleZero(MpptData& out){
  if (out.updated == 0) {
    out.vb = 0; out.vp = 0; out.ib = 0;
    out.temp_fet = 0; out.temp_coil = 0;
    out.ev_target = 0;
    out.duty_cycle = 0;
    return;
  }
  if ((uint32_t)(millis() - out.updated) > 10000UL) {
    out.vb = 0; out.vp = 0; out.ib = 0;
    out.temp_fet = 0; out.temp_coil = 0;
    out.ev_target = 0;
    out.duty_cycle = 0;
  }
}

void MpptRS485::request(uint16_t mpptAddr, uint8_t command){
  uint8_t p[25];
  p[0] = 'S';
  p[1] = mpptAddr >> 8; p[2] = mpptAddr & 0xFF;
  p[3] = _src >> 8;     p[4] = _src & 0xFF;
  p[5] = command;
  p[6] = 0;

  p[7]=0; p[8]=0; p[9]=0; p[10]=0; p[11]=0;
  for(int i=12;i<22;i++) p[i] = 0xAA;

  uint16_t crc = 0;
  for(int i=1;i<22;i++) crc += p[i];
  p[22] = crc >> 8; p[23] = crc & 0xFF;
  p[24] = 'E';

  Serial.print("TX -> MPPT");
  Serial.print(mpptAddr);
  Serial.print(" CMD:");
  Serial.print(command);
  Serial.print(" (25 bytes): ");
  for(int i = 0; i < 25; i++) {
    if(p[i] < 16) Serial.print("0");
    Serial.print(p[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  while(_s.available()) (void)_s.read();

  txMode();
  _s.write(p, 25);
  _s.flush();
  delayMicroseconds(150);
  rxMode();
}

void MpptRS485::startRead(uint16_t expectedId, bool isSettings){
  reading = true;
  readT0 = millis();
  readLastByte = millis();
  readN = 0;
  readExpectedId = expectedId;
  readIsSettings = isSettings;
}

bool MpptRS485::updateRead(){
  if (!reading) return false;

  uint32_t now = millis();

  if (now - readT0 >= 200) {
    processRead();
    reading = false;
    return true;
  }

  while(_s.available()){
    uint8_t b = (uint8_t)_s.read();
    readLastByte = now;
    if(readN < (int)sizeof(readBuf)) readBuf[readN++] = b;
    else { memmove(readBuf, readBuf+1, sizeof(readBuf)-1); readBuf[sizeof(readBuf)-1] = b; }
  }

  if(readN > 0 && now - readLastByte >= 10) {
    processRead();
    reading = false;
    return true;
  }

  return false;
}

bool MpptRS485::processRead(){
  if (readN > 0) {
    Serial.print(readIsSettings ? "RX <- VIEW_SETTINGS (" : "RX <- MPPT_DATA (");
    Serial.print(readN);
    Serial.print(" bytes): ");
    for (int i = 0; i < readN; i++) {
      if (readBuf[i] < 16) Serial.print("0");
      Serial.print(readBuf[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  }

  if(readN < 25) {
    Serial.println("  ERROR: Too few bytes received (< 25)");
    if(!readIsSettings){
      mpptData.valid = false;
      mpptData.err = 1;
      staleZero(mpptData);
    } else {
      mpptSettings.valid = false;
      mpptSettings.err = 1;
    }
    return false;
  }

  bool found = false;
  uint8_t bestErr = 1;

  for(int e = 24; e < readN; e++){
    if(readBuf[e] != 0x45) continue;

    uint8_t f[25];
    memcpy(f, &readBuf[e-24], 25);
    
    if(f[0] != 0x53) continue;
    if(f[24] != 0x45) continue;

    Serial.print("  Frame found: ");
    for(int i = 0; i < 25; i++) {
      if(f[i] < 16) Serial.print("0");
      Serial.print(f[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    if (readIsSettings) {
      if(f[5] != 8){
        Serial.println("  SETTINGS CMD ERROR!");
        bestErr = 4;
        continue;
      }
    } else {
      if(f[5] != 2 && f[5] != 4){
        Serial.println("  DATA CMD ERROR!");
        bestErr = 4;
        continue;
      }
    }

    uint16_t calc = sum16(&f[1], 21);
    uint16_t recv = u16be(f[22], f[23]);
    
    Serial.print("  CRC check: Calc=0x");
    Serial.print(calc, HEX);
    Serial.print(" Recv=0x");
    Serial.println(recv, HEX);
    
    if(calc != recv){
      Serial.println("  CRC ERROR!");
      bestErr = 3;
      continue;
    }

    uint16_t srcId = u16be(f[3], f[4]);
    Serial.print("  Address: ");
    Serial.println(srcId);
    
    if(srcId != readExpectedId){
      Serial.println("  ADDR MISMATCH!");
      bestErr = 2;
      continue;
    }

    if (!readIsSettings) {
      mpptData.temp_fet  = f[7];
      mpptData.temp_coil = f[8];
      mpptData.vb = u16be(f[9],  f[10]);
      mpptData.ib = u16be(f[11], f[12]);
      mpptData.vp = u16be(f[13], f[14]);
      mpptData.ev_target = u16be(f[15], f[16]);
      mpptData.duty_cycle = f[17];

      mpptData.valid   = true;
      mpptData.err     = 0;
      mpptData.updated = millis();
      found = true;
      
      Serial.print("  ✓ PARSED OK: TempFET=");
      Serial.print(mpptData.temp_fet);
      Serial.print("°C TempCoil=");
      Serial.print(mpptData.temp_coil);
      Serial.print("°C Vb=");
      Serial.print(mpptData.vb);
      Serial.print(" Ib=");
      Serial.print(mpptData.ib);
      Serial.print(" Vp=");
      Serial.print(mpptData.vp);
      Serial.print(" EV=");
      Serial.print(mpptData.ev_target);
      Serial.print(" Duty=");
      Serial.print(mpptData.duty_cycle);
      Serial.println("%");
    } else {
      Serial.println("  === DETAILED SETTINGS PARSE ===");
      
      Serial.println("  ALL PAYLOAD BYTES:");
      for(int i = 7; i <= 21; i++) {
        Serial.print("  f[");
        Serial.print(i);
        Serial.print("]=0x");
        if(f[i] < 16) Serial.print("0");
        Serial.print(f[i], HEX);
        Serial.print(" (");
        Serial.print(f[i]);
        Serial.println(")");
      }
      
      mpptSettings.vmp_voltage = u16be(f[16], f[17]);
      Serial.print("  Vmp from f[16]:f[17] (MSB:LSB) = ");
      Serial.print(mpptSettings.vmp_voltage);
      Serial.println(" V");
      
      uint16_t alt1 = u16be(f[17], f[16]);
      Serial.print("  Alt: f[17]:f[16] (MSB:LSB) = ");
      Serial.print(alt1);
      Serial.println(" V");
      
      uint16_t alt2 = f[16];
      Serial.print("  Alt: f[16] only (8-bit) = ");
      Serial.print(alt2);
      Serial.println(" V");
      
      uint16_t alt3 = f[17];
      Serial.print("  Alt: f[17] only (8-bit) = ");
      Serial.print(alt3);
      Serial.println(" V");
      
      uint16_t alt4 = u16be(f[15], f[16]);
      Serial.print("  Alt: f[15]:f[16] (MSB:LSB) = ");
      Serial.print(alt4);
      Serial.println(" V");
      
      uint16_t alt5 = u16be(f[14], f[15]);
      Serial.print("  Alt: f[14]:f[15] (MSB:LSB) = ");
      Serial.print(alt5);
      Serial.println(" V");

      mpptSettings.charge_limit = f[21];
      Serial.print("  Charge Limit from f[21] = ");
      Serial.print(mpptSettings.charge_limit);
      Serial.println(" %");
      
      Serial.print("  Alt: f[20] = ");
      Serial.print(f[20]);
      Serial.print(" %,  f[19] = ");
      Serial.print(f[19]);
      Serial.println(" %");

      mpptSettings.valid   = true;
      mpptSettings.err     = 0;
      mpptSettings.updated = millis();
      found = true;

      Serial.println("  ===============================");
      Serial.print("  ✓ PARSED - CURRENT Vmp=");
      Serial.print(mpptSettings.vmp_voltage);
      Serial.print("V Limit=");
      Serial.print(mpptSettings.charge_limit);
      Serial.println("%");
    }

    if (found) break;
  }

  if(!found){
    Serial.print("  PARSE FAILED - bestErr=");
    Serial.println(bestErr);
    if(!readIsSettings){
      mpptData.err = bestErr;
      staleZero(mpptData);
    } else {
      mpptSettings.err = bestErr;
    }
  }

  return found;
}

bool MpptRS485::sendSettings(uint16_t mpptAddr,
                             uint16_t vmpVoltage,
                             uint8_t chargeLimitPercent){
  for(int i = 0; i<3;i++){
    uint8_t p[25] = {0};
    p[0] = 'S';
    p[1] = mpptAddr >> 8;
    p[2] = mpptAddr & 0xFF;
    p[3] = _src >> 8;
    p[4] = _src & 0xFF;
    p[5] = 5;
    p[6] = 0;
    
    p[16] = vmpVoltage >> 8;
    p[17] = vmpVoltage & 0xFF;
    
    p[21] = chargeLimitPercent;

    uint16_t crc = 0;
    for (int i = 1; i < 22; i++) crc += p[i];
    p[22] = crc >> 8;
    p[23] = crc & 0xFF;
    p[24] = 'E';

    Serial.println();
    Serial.println("========================================");
    Serial.print("TX SETTINGS -> MPPT");
    Serial.print(mpptAddr);
    Serial.print(" (Attempt ");
    Serial.print(i+1);
    Serial.println("/3)");
    Serial.print("  Vmp=");
    Serial.print(vmpVoltage);
    Serial.print("V (0x");
    Serial.print(vmpVoltage, HEX);
    Serial.print(")");
    Serial.print(" Charge=");
    Serial.print(chargeLimitPercent);
    Serial.println("%");
    
    Serial.println("  FULL PACKET (25 bytes):");
    Serial.print("  ");
    for(int j = 0; j < 25; j++) {
      if(p[j] < 16) Serial.print("0");
      Serial.print(p[j], HEX);
      Serial.print(" ");
      if((j+1) % 8 == 0) Serial.print("\n  ");
    }
    Serial.println();
    
    Serial.println("  KEY POSITIONS:");
    Serial.print("  p[16] (Vmp MSB) = 0x");
    if(p[16] < 16) Serial.print("0");
    Serial.print(p[16], HEX);
    Serial.print(" (");
    Serial.print(p[16]);
    Serial.println(")");
    
    Serial.print("  p[17] (Vmp LSB) = 0x");
    if(p[17] < 16) Serial.print("0");
    Serial.print(p[17], HEX);
    Serial.print(" (");
    Serial.print(p[17]);
    Serial.println(")");
    
    Serial.print("  p[21] (Charge%) = 0x");
    if(p[21] < 16) Serial.print("0");
    Serial.print(p[21], HEX);
    Serial.print(" (");
    Serial.print(p[21]);
    Serial.println(")");
    Serial.println("========================================");

    while (_s.available()) (void)_s.read();

    txMode();
    _s.write(p, 25);
    _s.flush();
    delayMicroseconds(200);
    rxMode();
  }
  return true;
}