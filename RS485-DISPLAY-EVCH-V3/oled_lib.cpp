#include "oled_lib.h"
#include <stdio.h>
#include <string.h>

extern uint16_t vmpVoltage;
extern uint8_t chargeLimit;

// Static variables to track duty cycle sampling (Home page)
static uint8_t duty_cycle_samples[10];
static uint8_t sample_index = 0;
static uint8_t max_duty_in_window = 0;

// Static variables to track power sampling for delta (Home page)
static uint32_t power_samples[10] = {0};
static uint8_t power_sample_index = 0;
static uint32_t power_last_updated = 0;
static uint32_t power_blink_time = 0;

// Static variables to track max battery voltage sampling (Instantaneous page)
static uint16_t vb_samples[10];
static uint8_t vb_sample_index = 0;
static uint16_t max_vb_in_window = 0;

// Static variables to track instantaneous power sampling for delta (Instantaneous page)
static uint32_t inst_power_samples[10] = {0};
static uint8_t inst_power_sample_index = 0;
static uint32_t inst_power_last_updated = 0;
static uint32_t inst_power_blink_time = 0;

// Static variables to track instantaneous BV sampling for delta (Instantaneous page, status V)
static uint16_t inst_vb_delta_samples[10] = {0};
static uint8_t inst_vb_delta_sample_index = 0;

// Static variables to track instantaneous duty cycle max (Instantaneous page)
static uint8_t inst_duty_samples[10] = {0};
static uint8_t inst_duty_sample_index = 0;
static uint8_t inst_max_duty_in_window = 0;

// Last updated timestamp for duty cycle (Home page)
static uint32_t duty_last_updated = 0;

// Last updated timestamp for vb max (Instantaneous page)
static uint32_t vb_last_updated = 0;

// Helper: format power into a fixed-width 4-character + unit string
// 0-999W:       "0003W"  (4 digits + W)
// 1000-9999W:   "1.25kW" (X.XX + kW)
// 10000-99999W: "10.0kW" (XX.X + kW)
// 100000W+:     " 100kW" (3 digits + kW)
static void formatPower(char* buf, uint8_t bufSize, uint32_t watts) {
  if (watts < 1000) {
    snprintf(buf, bufSize, "%04luW", watts);
  } else if (watts < 10000) {
    uint16_t whole = (uint16_t)(watts / 1000);
    uint16_t frac = (uint16_t)((watts % 1000) / 10);
    snprintf(buf, bufSize, "%u.%02ukW", whole, frac);
  } else if (watts < 100000UL) {
    uint16_t whole = (uint16_t)(watts / 1000);
    uint16_t frac = (uint16_t)((watts % 1000) / 100);
    snprintf(buf, bufSize, "%u.%ukW", whole, frac);
  } else {
    uint16_t whole = (uint16_t)(watts / 1000);
    snprintf(buf, bufSize, "%ukW", whole);
  }
}

OledUI::OledUI(U8G2& display, uint8_t i2cAddr)
: _u8(display), _addr(i2cAddr) {}

bool OledUI::i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

bool OledUI::begin() {
  Wire.begin();
  Wire.setClock(100000UL);
  (void)i2cPresent(_addr);
  _u8.setI2CAddress(_addr << 1);
  return _u8.begin();
}

void OledUI::drawHomePage(const MpptData& d) {
  _u8.clearBuffer();
  _u8.setFont(u8g2_font_6x12_tf);
  _u8.drawStr(0, 12, "Home");

  char s[40];

  // Current in centi-amps: d.ib * 12.5 = d.ib * 125 / 10
  uint32_t ib_centi = ((uint32_t)d.ib * 125UL + 5) / 10;
  uint16_t ib_i = (uint16_t)(ib_centi / 100);
  uint8_t  ib_d = (uint8_t)(ib_centi % 100);

  // Power = Vb * current_in_amps = Vb * ib_centi / 100
  uint32_t power = ((uint32_t)d.vb * ib_centi + 50) / 100;

  char pw[16];
  formatPower(pw, sizeof(pw), power);
  snprintf(s, sizeof(s), ":%s", pw);
  _u8.drawStr(50, 12, s);

  _u8.drawHLine(0, 14, 128);
  _u8.setFont(u8g2_font_5x8_tf);

  if (d.valid) {
    // Only sample when new data has arrived
    bool newData = (d.updated != duty_last_updated);

    if (newData) {
      duty_last_updated = d.updated;

      // Add new duty cycle sample (from command 9 average data)
      duty_cycle_samples[sample_index] = d.duty_cycle;
      sample_index = (sample_index + 1) % 10;

      // Add new power sample
      power_samples[power_sample_index] = power;
      power_sample_index = (power_sample_index + 1) % 10;

      // Trigger blink when window completes
      if (power_sample_index == 0) {
        power_blink_time = millis();
      }
    }

    // Calculate min, max duty cycle across the full 10-sample window
    uint8_t min_duty_in_window = duty_cycle_samples[0];
    max_duty_in_window = duty_cycle_samples[0];
    for (uint8_t i = 1; i < 10; i++) {
      if (duty_cycle_samples[i] < min_duty_in_window) {
        min_duty_in_window = duty_cycle_samples[i];
      }
      if (duty_cycle_samples[i] > max_duty_in_window) {
        max_duty_in_window = duty_cycle_samples[i];
      }
    }

    // Calculate min, max power across the full 10-sample window
    uint32_t min_power = power_samples[0];
    uint32_t max_power = power_samples[0];
    for (uint8_t i = 1; i < 10; i++) {
      if (power_samples[i] < min_power) {
        min_power = power_samples[i];
      }
      if (power_samples[i] > max_power) {
        max_power = power_samples[i];
      }
    }

    // Calculate power delta: (difference / average) * 100
    uint32_t power_diff = max_power - min_power;
    uint32_t power_sum = min_power + max_power;
    uint16_t power_delta = 0;
    if (power_sum > 0) {
      power_delta = (uint16_t)((power_diff * 200 + power_sum / 2) / power_sum);
    }

    // Home page always uses power-based delta, so apply 80W threshold
    if (power < 80) {
      power_delta = 0;
    }

    snprintf(s, sizeof(s), "PV: %u V", d.vp);
    _u8.drawStr(4, 28, s);
    
    snprintf(s, sizeof(s), "BV: %u V", d.vb);
    _u8.drawStr(4, 40, s);
    
    snprintf(s, sizeof(s), "BI: %u.%02u A", ib_i, ib_d);
    _u8.drawStr(4, 52, s);

    snprintf(s, sizeof(s), "Vs: %u V", d.vset);
    _u8.drawStr(4, 64, s);

    // ---- Right column: fixed left margin, aligned colons ----
    const uint8_t labelX = 78;
    const uint8_t colonX = 98;

    // Row 28: Duty (from command 9 average data)
    _u8.drawStr(labelX, 28, "Duty");
    snprintf(s, sizeof(s), ":%02u%%", d.duty_cycle);
    _u8.drawStr(colonX, 28, s);

    // Row 40: Δ (delta triangle symbol) with blink behavior
    if (power_sample_index != 0) {
      _u8.drawTriangle(labelX + 3, 34, labelX, 40, labelX + 6, 40);
    }
    snprintf(s, sizeof(s), ":%u%%", power_delta);
    _u8.drawStr(colonX, 40, s);

    // Row 52: Max
    _u8.drawStr(labelX, 52, "Max");
    snprintf(s, sizeof(s), ":%02u%%", max_duty_in_window);
    _u8.drawStr(colonX, 52, s);

  } else {
    _u8.drawStr(4, 40, "No data");
  }

  _u8.sendBuffer();
}

void OledUI::drawMoreOptions(uint8_t selected) {
  _u8.clearBuffer();
  _u8.setFont(u8g2_font_6x12_tf);
  _u8.drawStr(0, 12, "OPTIONS");
  _u8.drawHLine(0, 14, 128);

  const char* items[] = {
    "Instantaneous Data",
    "Edit Settings",
    "View Settings",
    "-- HOME --"
  };
  uint8_t count = 4;

  _u8.setFont(u8g2_font_5x8_tf);
  for (uint8_t i = 0; i < count; i++) {
    uint8_t y = 24 + i * 10;
    bool sel = (i == selected);
    if (sel) {
      _u8.drawBox(0, y - 7, 128, 9);
      _u8.setDrawColor(0);
    }
    _u8.drawStr(4, y, items[i]);
    if (sel) _u8.setDrawColor(1);
  }
  _u8.sendBuffer();
}

void OledUI::drawInstantaneousMpptCard(const MpptData& d) {
  _u8.clearBuffer();
  _u8.setFont(u8g2_font_6x12_tf);

  char s[40];

  _u8.drawHLine(0, 14, 128);

  if (d.valid) {
    // Apply scaling formula: (511 - instant_value) * 0.24242424
    int16_t diff = 511 - (int16_t)d.ib;
    
    // Calculate current in centi-amps (hundredths of an amp)
    int32_t current_centi = ((int32_t)diff * 12121212L + 500000L) / 1000000L;
    
    // Handle sign for current display
    bool neg = current_centi < 0;
    uint32_t abs_centi = neg ? (uint32_t)(-current_centi) : (uint32_t)current_centi;
    uint16_t ib_i = (uint16_t)(abs_centi / 100);
    uint8_t  ib_d = (uint8_t)(abs_centi % 100);

    // Power = Vb * current_centi / 100, using abs value and rounding
    uint32_t power = ((uint32_t)d.vb * abs_centi + 50) / 100;

    // Only sample when new data has arrived
    bool newData = (d.updated != inst_power_last_updated);

    if (newData) {
      inst_power_last_updated = d.updated;

      // Track max battery voltage in a 10-sample sliding window
      vb_samples[vb_sample_index] = d.vb;
      vb_sample_index = (vb_sample_index + 1) % 10;

      // Track instantaneous power in a 10-sample sliding window for delta
      inst_power_samples[inst_power_sample_index] = power;
      inst_power_sample_index = (inst_power_sample_index + 1) % 10;

      // Track battery voltage in a 10-sample sliding window for delta (status V)
      inst_vb_delta_samples[inst_vb_delta_sample_index] = d.vb;
      inst_vb_delta_sample_index = (inst_vb_delta_sample_index + 1) % 10;

      // Track instantaneous duty cycle in a 10-sample sliding window for max
      // Uses inst_duty_cycle from command 3 byte 15
      inst_duty_samples[inst_duty_sample_index] = d.inst_duty_cycle;
      inst_duty_sample_index = (inst_duty_sample_index + 1) % 10;

      // Trigger blink when power window completes
      if (inst_power_sample_index == 0) {
        inst_power_blink_time = millis();
      }
    }

    // Calculate max battery voltage across the full 10-sample window
    max_vb_in_window = vb_samples[0];
    for (uint8_t i = 1; i < 10; i++) {
      if (vb_samples[i] > max_vb_in_window) {
        max_vb_in_window = vb_samples[i];
      }
    }

    // Calculate max duty cycle across the full 10-sample window (Instantaneous page)
    inst_max_duty_in_window = inst_duty_samples[0];
    for (uint8_t i = 1; i < 10; i++) {
      if (inst_duty_samples[i] > inst_max_duty_in_window) {
        inst_max_duty_in_window = inst_duty_samples[i];
      }
    }

    // Calculate power delta from power samples
    uint32_t inst_min_power = inst_power_samples[0];
    uint32_t inst_max_power = inst_power_samples[0];
    for (uint8_t i = 1; i < 10; i++) {
      if (inst_power_samples[i] < inst_min_power) {
        inst_min_power = inst_power_samples[i];
      }
      if (inst_power_samples[i] > inst_max_power) {
        inst_max_power = inst_power_samples[i];
      }
    }

    uint32_t inst_power_diff = inst_max_power - inst_min_power;
    uint32_t inst_power_sum = inst_min_power + inst_max_power;
    uint16_t inst_power_delta = 0;
    if (inst_power_sum > 0) {
      inst_power_delta = (uint16_t)((inst_power_diff * 200 + inst_power_sum / 2) / inst_power_sum);
    }

    // Calculate BV delta from battery voltage samples
    uint16_t inst_min_vb = inst_vb_delta_samples[0];
    uint16_t inst_max_vb = inst_vb_delta_samples[0];
    for (uint8_t i = 1; i < 10; i++) {
      if (inst_vb_delta_samples[i] < inst_min_vb) {
        inst_min_vb = inst_vb_delta_samples[i];
      }
      if (inst_vb_delta_samples[i] > inst_max_vb) {
        inst_max_vb = inst_vb_delta_samples[i];
      }
    }

    uint32_t inst_vb_diff = (uint32_t)(inst_max_vb - inst_min_vb);
    uint32_t inst_vb_sum = (uint32_t)inst_min_vb + (uint32_t)inst_max_vb;
    uint16_t inst_vb_delta = 0;
    if (inst_vb_sum > 0) {
      inst_vb_delta = (uint16_t)((inst_vb_diff * 200 + inst_vb_sum / 2) / inst_vb_sum);
    }

    // Select which delta to display based on mppt_status
    // Status 0 (S) / 4 (E): always 0%
    // Status 1 (V): BV delta (voltage-based, no 80W threshold)
    // Status 2 (I) / 3 (T) / default: power delta, but 0% if power < 80W
    uint16_t inst_display_delta = 0;
    if (d.mppt_status == 0 || d.mppt_status == 4) {
      inst_display_delta = 0;
    } else if (d.mppt_status == 1) {
      inst_display_delta = inst_vb_delta;
    } else {
      // Power-based delta: suppress if below 80W
      if (power < 80) {
        inst_display_delta = 0;
      } else {
        inst_display_delta = inst_power_delta;
      }
    }

    // Determine status indicator character from mppt_status
    // 0 = S, 1 = V, 2 = I, 3 = T, 4 = E, 5 = P
    char status_char = 'S';
    switch (d.mppt_status) {
      case 0: status_char = 'S'; break;
      case 1: status_char = 'V'; break;
      case 2: status_char = 'I'; break;
      case 3: status_char = 'T'; break;
      case 4: status_char = 'E'; break;
      case 5: status_char = 'P'; break;
      default: status_char = '?'; break;
    }

    // Display power with status indicator
    char pw[16];
    formatPower(pw, sizeof(pw), power);
    snprintf(s, sizeof(s), "%s (%c)", pw, status_char);
    _u8.drawStr(0, 12, s);

    // Display delta with fixed-position Δ triangle in the header
    // Blink off for 1 second when 10-sample window completes
    if (inst_power_sample_index != 0) {
      _u8.drawTriangle(68, 4, 65, 12, 71, 12);
    }
    snprintf(s, sizeof(s), ":%u%%", inst_display_delta);
    _u8.drawStr(73, 12, s);

    // Display age on the far right
    uint32_t age = millis() - d.updated;
    snprintf(s, sizeof(s), "%lums", (unsigned long)age);
    int w = _u8.getStrWidth(s);
    _u8.drawStr(126 - w, 12, s);

    _u8.setFont(u8g2_font_5x8_tf);

    snprintf(s, sizeof(s), "PV:%u V", d.vp);
    _u8.drawStr(0, 26, s);
    snprintf(s, sizeof(s), "C:%u%c", d.temp_coil, 0xB0);
    _u8.drawStr(70, 26, s);

    snprintf(s, sizeof(s), "BV:%u V", d.vb);
    _u8.drawStr(0, 38, s);
    snprintf(s, sizeof(s), "F:%u%c", d.temp_fet, 0xB0);
    _u8.drawStr(70, 38, s);

    snprintf(s, sizeof(s), "BI:%s%u.%02u A", neg ? "-" : "", ib_i, ib_d);
    _u8.drawStr(0, 50, s);

    // Display PWM minimum
    snprintf(s, sizeof(s), "%%min:%u", d.pwm_min);
    _u8.drawStr(70, 50, s);

    // Display instantaneous duty cycle with max in brackets
    // Uses inst_duty_cycle from command 3 byte 15
    snprintf(s, sizeof(s), "DC:%02u%% (%02u%%)", d.inst_duty_cycle, inst_max_duty_in_window);
    _u8.drawStr(0, 62, s);

    // Display max battery voltage from current 10-sample window
    snprintf(s, sizeof(s), "Mx:%u V", max_vb_in_window);
    _u8.drawStr(70, 62, s);
  } else {
    _u8.drawStr(0, 12, "Inst");
    _u8.setFont(u8g2_font_5x8_tf);
    _u8.drawStr(0, 40, "No data");
  }
  _u8.sendBuffer();
}

void OledUI::drawEditSettingsPage(uint8_t selected, bool editing) {
  _u8.clearBuffer();
  _u8.setFont(u8g2_font_6x12_tf);
  _u8.drawStr(0, 12, "EDIT SETTINGS");
  _u8.drawHLine(0, 14, 128);

  _u8.setFont(u8g2_font_5x8_tf);

  const char* items[] = {
    "Vmp Voltage", "Charge Limit %", "Save Changes", "EXIT"
  };

  char val[4][16];
  snprintf(val[0], sizeof(val[0]), "%u V", vmpVoltage);
  snprintf(val[1], sizeof(val[1]), "%d%%", chargeLimit);
  strcpy(val[2], "");
  strcpy(val[3], "");

  for (uint8_t i = 0; i < 4; i++) {
    uint8_t y = 24 + i * 10;
    bool sel = (i == selected);
    if (sel) {
      _u8.drawBox(0, y - 7, 128, 9);
      _u8.setDrawColor(0);
    }
    _u8.drawStr(4, y, items[i]);

    if (i < 2) {
      int w = _u8.getStrWidth(val[i]);
      int xv = 126 - w;

      if (editing && sel) {
        if ((millis() / 250) % 2 == 0) {
          _u8.setDrawColor(1);
          _u8.drawBox(xv - 2, y - 7, w + 4, 9);
          _u8.setDrawColor(0);
          _u8.drawStr(xv, y, val[i]);
          _u8.setDrawColor(1);
        } else {
          _u8.drawFrame(xv - 2, y - 7, w + 4, 9);
          _u8.drawStr(xv, y, val[i]);
        }
      } else {
        _u8.drawStr(xv, y, val[i]);
      }
    }

    if (sel) _u8.setDrawColor(1);
  }

  _u8.sendBuffer();
}

void OledUI::drawMpptSettingsPage(uint8_t mpptId, const MpptSettings& d) {
  _u8.clearBuffer();
  _u8.setFont(u8g2_font_6x12_tf);
  char s[32];
  snprintf(s, sizeof(s), "MPPT%u SETTINGS", mpptId);
  _u8.drawStr(0, 12, s);
  _u8.drawHLine(0, 14, 128);

  _u8.setFont(u8g2_font_5x8_tf);

  if (d.valid) {
    snprintf(s, sizeof(s), "Vmp V    : %u V", d.vmp_voltage);
    _u8.drawStr(2, 30, s);

    snprintf(s, sizeof(s), "Chg Limit: %d%%", d.charge_limit);
    _u8.drawStr(2, 45, s);
  } else {
    _u8.drawStr(2, 32, "No data received");
    snprintf(s, sizeof(s), "ERR: %u", d.err);
    _u8.drawStr(2, 46, s);
  }

  _u8.sendBuffer();
}

void OledUI::drawSavingMessage() {
  _u8.clearBuffer();
  _u8.setFont(u8g2_font_6x12_tf);
  _u8.drawStr(24, 32, "Saving...");
  _u8.drawStr(8, 48, "Please wait");
  _u8.sendBuffer();
}