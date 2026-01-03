//  TM1637Display32 - Non-blocking version with timing support for ESP32 and RP2040
//
//  Original Author: avishorp@gmail.com
//  Non-blocking fork: cphouser
//  ESP32/RP2040 timing fixes: algomusic 2025
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.

extern "C" {
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
}

#include <TM1637Display32.h>
#include <Arduino.h>

#define TM1637_I2C_COMM1    0x40
#define TM1637_I2C_COMM2    0xC0
#define TM1637_I2C_COMM3    0x80

// Minimum microseconds between state changes
// TM1637 datasheet specifies ~1µs minimum, but we use more for reliability
// ESP32/RP2040 require longer delays for reliable non-blocking operation
#if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
#define BIT_DELAY_US 50  // Fast MCUs need explicit timing for reliable non-blocking updates
#else
#define BIT_DELAY_US 0    // AVR is slow enough that no delay needed
#endif

const uint8_t digitToSegment[] = {
  // XGFEDCBA
  0b00111111,    // 0
  0b00000110,    // 1
  0b01011011,    // 2
  0b01001111,    // 3
  0b01100110,    // 4
  0b01101101,    // 5
  0b01111101,    // 6
  0b00000111,    // 7
  0b01111111,    // 8
  0b01101111,    // 9
  0b01110111,    // A
  0b01111100,    // b
  0b00111001,    // C
  0b01011110,    // d
  0b01111001,    // E
  0b01110001     // F
};

static const uint8_t minusSegments = 0b01000000;

// Character segment patterns: A-Z (0-25), 0-9 (26-35), space (36), dash (37)
static const uint8_t charToSegment[] = {
  SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,         // A
  SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,                 // b
  SEG_A | SEG_D | SEG_E | SEG_F,                         // C
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,                 // d
  SEG_A | SEG_D | SEG_E | SEG_F | SEG_G,                 // E
  SEG_A | SEG_E | SEG_F | SEG_G,                         // F
  SEG_A | SEG_C | SEG_D | SEG_E | SEG_F,                 // G
  SEG_C | SEG_E | SEG_F | SEG_G,                         // h
  SEG_E | SEG_F,                                         // I
  SEG_B | SEG_C | SEG_D | SEG_E,                         // J
  SEG_C | SEG_E | SEG_F | SEG_G,                         // k (same as h)
  SEG_D | SEG_E | SEG_F,                                 // L
  SEG_A | SEG_C | SEG_E | SEG_G,                         // M (stylized)
  SEG_C | SEG_E | SEG_G,                                 // n
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,         // O
  SEG_A | SEG_B | SEG_E | SEG_F | SEG_G,                 // P
  SEG_A | SEG_B | SEG_C | SEG_F | SEG_G,                 // q
  SEG_E | SEG_G,                                         // r
  SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                 // S
  SEG_D | SEG_E | SEG_F | SEG_G,                         // t
  SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,                 // U
  SEG_C | SEG_D | SEG_E,                                 // v
  SEG_B | SEG_D | SEG_F | SEG_G,                         // W (stylized)
  SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,                 // X (same as H)
  SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,                 // y
  SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                 // Z
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,         // 0
  SEG_B | SEG_C,                                         // 1
  SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                 // 2
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                 // 3
  SEG_B | SEG_C | SEG_F | SEG_G,                         // 4
  SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                 // 5
  SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,         // 6
  SEG_A | SEG_B | SEG_C,                                 // 7
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, // 8
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,         // 9
  0x00,                                                  // space
  SEG_G                                                  // dash
};

TM1637Display32::TM1637Display32(uint8_t pinClk, uint8_t pinDIO) {
  m_pinClk = pinClk;
  m_pinDIO = pinDIO;
  m_brightness = 0x0F;  // Max brightness (7) + display ON (0x08)
  m_counter = 255;  // Idle state (no transmission pending)
  m_lastUpdateMicros = 0;
  m_transmissionStartMillis = 0;

  // Pre-set output latch to LOW (for when we switch to OUTPUT mode)
  digitalWrite(m_pinClk, LOW);
  digitalWrite(m_pinDIO, LOW);

  // Both pins are set as inputs with pull-ups for open-drain signaling
  #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
  pinMode(m_pinClk, INPUT_PULLUP);
  pinMode(m_pinDIO, INPUT_PULLUP);
  #else
  pinMode(m_pinClk, INPUT);
  pinMode(m_pinDIO, INPUT);
  #endif
}

void TM1637Display32::setBrightness(uint8_t brightness, bool on) {
  m_brightness = (brightness & 0x7) | (on ? 0x08 : 0x00);
}

void TM1637Display32::setSegments(const uint8_t segments[], uint8_t length, uint8_t pos) {
  // Keep state machine idle during setup to prevent ISR conflicts
  m_counter = 255;

  // Copy segment data
  memset(m_segments, 0, 4);
  if (length > 4) length = 4;
  memcpy(m_segments, segments, length);

  m_pos = pos;
  m_length = length;

  // Force a clean stop condition to terminate any in-progress transaction
  // This prevents the TM1637 from getting stuck waiting for more data
  // Stop sequence: CLK LOW -> DIO LOW -> CLK HIGH -> DIO HIGH
  pinMode(m_pinClk, OUTPUT);
  digitalWrite(m_pinClk, LOW);
  delayMicroseconds(5);
  pinMode(m_pinDIO, OUTPUT);
  digitalWrite(m_pinDIO, LOW);
  delayMicroseconds(5);
  #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
  pinMode(m_pinClk, INPUT_PULLUP);  // CLK HIGH
  #else
  pinMode(m_pinClk, INPUT);
  #endif
  delayMicroseconds(5);
  #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
  pinMode(m_pinDIO, INPUT_PULLUP);  // DIO HIGH (stop condition)
  #else
  pinMode(m_pinDIO, INPUT);
  #endif
  delayMicroseconds(1200);  // Datasheet: reset both lines high for >1ms after error

  // Start condition: DIO goes LOW while CLK is HIGH
  pinMode(m_pinDIO, OUTPUT);
  digitalWrite(m_pinDIO, LOW);
  delayMicroseconds(10);  // Let TM1637 recognize start condition

  m_lastUpdateMicros = micros();
  m_transmissionStartMillis = millis();  // For watchdog timeout

  // NOW enable state machine - after all blocking GPIO work is done
  // This prevents ISR from conflicting with the setup sequence above
  m_phase = 0;
  m_bit_count = 0;
  m_byte = TM1637_I2C_COMM1;
  m_counter = 0;  // Start transmission (must be last!)
}

bool TM1637Display32::update() {
  // Check if transmission is complete or idle
  if (m_counter == 255) {
    return true;  // No transmission in progress
  }

  // Watchdog: if transmission takes too long, reset to idle
  // A complete 4-digit transmission should take ~7ms max at 100µs per step
  // Allow 50ms as generous timeout for worst-case scenarios
  unsigned long nowMillis = millis();
  if ((nowMillis - m_transmissionStartMillis) > 50) {
    m_counter = 255;  // Force idle - transmission timed out
    return true;
  }

  // Rate limiting: ensure minimum time between state changes
  #if BIT_DELAY_US > 0
  unsigned long now = micros();
  if ((now - m_lastUpdateMicros) < BIT_DELAY_US) {
    return false;  // Not enough time elapsed, try again later
  }
  m_lastUpdateMicros = now;
  #endif

  // State machine for TM1637 protocol
  // Protocol: START -> COMM1 -> STOP -> START -> COMM2+addr -> DATA bytes -> STOP -> START -> COMM3 -> STOP

  switch (m_phase) {
    case 0:  // Write COMM1 byte (0x40 = write data)
      if (writeBit()) {
        m_phase = 1;
        m_counter = 0;
      }
      break;

    case 1:  // Stop condition after COMM1
      if (stopCondition()) {
        m_phase = 2;
        m_counter = 0;
        m_byte = TM1637_I2C_COMM2 + (m_pos & 0x03);  // Address command
      }
      break;

    case 2:  // Start condition before COMM2
      if (startCondition()) {
        m_phase = 3;
        m_counter = 0;
      }
      break;

    case 3:  // Write COMM2 byte (address)
      if (writeBit()) {
        m_phase = 4;
        m_counter = 0;
        m_currentSegment = 0;
        if (m_length > 0) {
          m_byte = m_segments[0];
        }
      }
      break;

    case 4:  // Write segment data bytes
      if (writeBit()) {
        m_currentSegment++;
        if (m_currentSegment >= m_length) {
          m_phase = 5;
          m_counter = 0;
        } else {
          m_byte = m_segments[m_currentSegment];
          m_counter = 0;
        }
      }
      break;

    case 5:  // Stop condition after data
      if (stopCondition()) {
        m_phase = 6;
        m_counter = 0;
        m_byte = TM1637_I2C_COMM3 + (m_brightness & 0x0f);  // Display control
      }
      break;

    case 6:  // Start condition before COMM3
      if (startCondition()) {
        m_phase = 7;
        m_counter = 0;
      }
      break;

    case 7:  // Write COMM3 byte (display control)
      if (writeBit()) {
        m_phase = 8;
        m_counter = 0;
      }
      break;

    case 8:  // Final stop condition
      if (stopCondition()) {
        m_counter = 255;  // Mark as complete
        return true;
      }
      break;
  }

  return false;
}

bool TM1637Display32::isIdle() const {
  return m_counter == 255;
}

// Write one bit of m_byte, returns true when byte complete (including ACK)
bool TM1637Display32::writeBit() {
  switch (m_counter) {
    case 0:  // CLK LOW
      pinMode(m_pinClk, OUTPUT);
      digitalWrite(m_pinClk, LOW);  // Explicit LOW for ESP32
      m_counter++;
      break;

    case 1:  // Set DIO to data bit
      if (m_byte & 0x01) {
        #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
        pinMode(m_pinDIO, INPUT_PULLUP);
        #else
        pinMode(m_pinDIO, INPUT);
        #endif
      } else {
        pinMode(m_pinDIO, OUTPUT);
        digitalWrite(m_pinDIO, LOW);  // Explicit LOW for ESP32
      }
      m_counter++;
      break;

    case 2:  // CLK HIGH (data is sampled by TM1637)
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      pinMode(m_pinClk, INPUT_PULLUP);
      #else
      pinMode(m_pinClk, INPUT);
      #endif
      m_byte >>= 1;
      m_bit_count++;
      if (m_bit_count < 8) {
        m_counter = 0;  // Loop back for next bit
      } else {
        m_counter++;  // Move to ACK phase
      }
      break;

    case 3:  // CLK LOW for ACK
      pinMode(m_pinClk, OUTPUT);
      digitalWrite(m_pinClk, LOW);  // Explicit LOW for ESP32
      m_counter++;
      break;

    case 4:  // Release DIO for ACK (we don't actually check it)
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      pinMode(m_pinDIO, INPUT_PULLUP);
      #else
      pinMode(m_pinDIO, INPUT);
      #endif
      m_counter++;
      break;

    case 5:  // CLK HIGH for ACK
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      pinMode(m_pinClk, INPUT_PULLUP);
      #else
      pinMode(m_pinClk, INPUT);
      #endif
      m_counter++;
      break;

    case 6:  // CLK LOW after ACK
      pinMode(m_pinClk, OUTPUT);
      digitalWrite(m_pinClk, LOW);  // Explicit LOW for ESP32
      m_bit_count = 0;
      return true;  // Byte complete
  }
  return false;
}

// Generate start condition, returns true when complete
bool TM1637Display32::startCondition() {
  switch (m_counter) {
    case 0:  // Ensure CLK is HIGH
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      pinMode(m_pinClk, INPUT_PULLUP);
      #else
      pinMode(m_pinClk, INPUT);
      #endif
      m_counter++;
      break;

    case 1:  // Ensure DIO is HIGH
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      pinMode(m_pinDIO, INPUT_PULLUP);
      #else
      pinMode(m_pinDIO, INPUT);
      #endif
      m_counter++;
      break;

    case 2:  // DIO goes LOW while CLK is HIGH (start condition)
      pinMode(m_pinDIO, OUTPUT);
      digitalWrite(m_pinDIO, LOW);  // Explicit LOW for ESP32
      return true;
  }
  return false;
}

// Generate stop condition, returns true when complete
bool TM1637Display32::stopCondition() {
  switch (m_counter) {
    case 0:  // CLK LOW
      pinMode(m_pinClk, OUTPUT);
      digitalWrite(m_pinClk, LOW);  // Explicit LOW for ESP32
      m_counter++;
      break;

    case 1:  // DIO LOW
      pinMode(m_pinDIO, OUTPUT);
      digitalWrite(m_pinDIO, LOW);  // Explicit LOW for ESP32
      m_counter++;
      break;

    case 2:  // CLK HIGH
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      pinMode(m_pinClk, INPUT_PULLUP);
      #else
      pinMode(m_pinClk, INPUT);
      #endif
      m_counter++;
      break;

    case 3:  // DIO HIGH (stop condition: DIO rises while CLK is HIGH)
      #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
      pinMode(m_pinDIO, INPUT_PULLUP);
      #else
      pinMode(m_pinDIO, INPUT);
      #endif
      return true;
  }
  return false;
}

void TM1637Display32::clear() {
  uint8_t data[] = { 0, 0, 0, 0 };
  setSegments(data);
}

void TM1637Display32::showNumberDec(int num, bool leading_zero, uint8_t length, uint8_t pos) {
  showNumberDecEx(num, 0, leading_zero, length, pos);
}

void TM1637Display32::showNumberDecEx(int num, uint8_t dots, bool leading_zero,
                                    uint8_t length, uint8_t pos) {
  showNumberBaseEx(num < 0 ? -10 : 10, num < 0 ? -num : num,
                   dots, leading_zero, length, pos);
}

void TM1637Display32::showNumberHexEx(uint16_t num, uint8_t dots,
                                    bool leading_zero, uint8_t length,
                                    uint8_t pos) {
  showNumberBaseEx(16, num, dots, leading_zero, length, pos);
}

void TM1637Display32::showNumberBaseEx(int8_t base, uint16_t num, uint8_t dots,
                                     bool leading_zero, uint8_t length,
                                     uint8_t pos) {
  bool negative = false;
  if (base < 0) {
    base = -base;
    negative = true;
  }
  uint8_t digits[4];

  if (num == 0 && !leading_zero) {
    for (uint8_t i = 0; i < (length - 1); i++)
      digits[i] = 0;
    digits[length - 1] = encodeDigit(0);
  } else {
    for (int i = length - 1; i >= 0; --i) {
      uint8_t digit = num % base;

      if (digit == 0 && num == 0 && leading_zero == false)
        digits[i] = 0;
      else
        digits[i] = encodeDigit(digit);

      if (digit == 0 && num == 0 && negative) {
        digits[i] = minusSegments;
        negative = false;
      }

      num /= base;
    }

    if (dots != 0) {
      showDots(dots, digits);
    }
  }
  setSegments(digits, length, pos);
}

void TM1637Display32::showDots(uint8_t dots, uint8_t* digits) {
  for (int i = 0; i < 4; ++i) {
    digits[i] |= (dots & 0x80);
    dots <<= 1;
  }
}

uint8_t TM1637Display32::encodeDigit(uint8_t digit) {
  return digitToSegment[digit & 0x0f];
}

uint8_t TM1637Display32::charToSeg(char c) {
  if (c >= 'A' && c <= 'Z') return charToSegment[c - 'A'];
  if (c >= 'a' && c <= 'z') return charToSegment[c - 'a'];
  if (c >= '0' && c <= '9') return charToSegment[26 + (c - '0')];
  if (c == ' ') return charToSegment[36];
  if (c == '-') return charToSegment[37];
  return 0x00;  // unknown character = blank
}

void TM1637Display32::displayText(const char* text, uint8_t pos) {
  uint8_t segs[4] = {0, 0, 0, 0};
  int maxLen = 4 - pos;
  int textLen = strlen(text);
  int len = (maxLen < textLen) ? maxLen : textLen;
  for (int i = 0; i < len; i++) {
    segs[pos + i] = charToSeg(text[i]);
  }
  setSegments(segs);
}

void TM1637Display32::displayCharAndNumber(char c, int number) {
  uint8_t segs[4];
  segs[0] = charToSeg(c);

  // Format number for 3 digits (positions 1-3)
  int absNum = abs(number);
  if (absNum >= 10000) {
    // 10000+: show as XX.X (e.g., 12300 -> "12.3")
    absNum = absNum / 100;
    segs[1] = encodeDigit((absNum / 100) % 10);
    segs[2] = encodeDigit((absNum / 10) % 10) | SEG_DP;
    segs[3] = encodeDigit(absNum % 10);
  } else if (absNum >= 1000) {
    // 1000-9999: show as X.XK (e.g., 1024 -> "1.0K", 5678 -> "5.6K")
    absNum = absNum / 100;  // Get first two significant digits
    segs[1] = encodeDigit((absNum / 10) % 10) | SEG_DP;
    segs[2] = encodeDigit(absNum % 10);
    segs[3] = charToSeg('K');
  } else {
    // 0-999: show as is, right-aligned, blank leading zeros
    segs[1] = (absNum >= 100) ? encodeDigit((absNum / 100) % 10) : 0;
    segs[2] = (absNum >= 10) ? encodeDigit((absNum / 10) % 10) : 0;
    segs[3] = encodeDigit(absNum % 10);
    // Handle negative
    if (number < 0 && absNum < 100) {
      segs[1] = SEG_G;  // Minus sign
    }
  }
  setSegments(segs, 4, 0);
}
