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
#define BIT_DELAY_US 100  // Fast MCUs need explicit timing for reliable non-blocking updates
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

TM1637Display::TM1637Display(uint8_t pinClk, uint8_t pinDIO) {
  m_pinClk = pinClk;
  m_pinDIO = pinDIO;
  m_brightness = 0x0F;  // Max brightness (7) + display ON (0x08)
  m_counter = 255;  // Idle state (no transmission pending)
  m_lastUpdateMicros = 0;

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

void TM1637Display::setBrightness(uint8_t brightness, bool on) {
  m_brightness = (brightness & 0x7) | (on ? 0x08 : 0x00);
}

void TM1637Display::setSegments(const uint8_t segments[], uint8_t length, uint8_t pos) {
  // Copy segment data
  memset(m_segments, 0, 4);
  if (length > 4) length = 4;
  memcpy(m_segments, segments, length);

  m_pos = pos;
  m_length = length;
  m_counter = 0;  // Start transmission
  m_phase = 0;
  m_bit_count = 0;
  m_byte = TM1637_I2C_COMM1;

  // Ensure bus is in idle state (both lines HIGH) before starting
  // This is critical for subsequent transmissions to be recognized
  #if defined(ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_RP2040)
  pinMode(m_pinDIO, INPUT_PULLUP);  // DIO HIGH first
  pinMode(m_pinClk, INPUT_PULLUP);  // CLK HIGH
  #else
  pinMode(m_pinDIO, INPUT);
  pinMode(m_pinClk, INPUT);
  #endif
  delayMicroseconds(50);  // Let bus settle to idle state (increased for reliability)

  // Start condition: DIO goes LOW while CLK is HIGH
  pinMode(m_pinDIO, OUTPUT);
  digitalWrite(m_pinDIO, LOW);  // Explicit LOW for ESP32
  delayMicroseconds(10);  // Let TM1637 recognize start condition

  m_lastUpdateMicros = micros();
}

bool TM1637Display::update() {
  // Check if transmission is complete or idle
  if (m_counter == 255) {
    return true;  // No transmission in progress
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

// Write one bit of m_byte, returns true when byte complete (including ACK)
bool TM1637Display::writeBit() {
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
bool TM1637Display::startCondition() {
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
bool TM1637Display::stopCondition() {
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

void TM1637Display::clear() {
  uint8_t data[] = { 0, 0, 0, 0 };
  setSegments(data);
}

void TM1637Display::showNumberDec(int num, bool leading_zero, uint8_t length, uint8_t pos) {
  showNumberDecEx(num, 0, leading_zero, length, pos);
}

void TM1637Display::showNumberDecEx(int num, uint8_t dots, bool leading_zero,
                                    uint8_t length, uint8_t pos) {
  showNumberBaseEx(num < 0 ? -10 : 10, num < 0 ? -num : num,
                   dots, leading_zero, length, pos);
}

void TM1637Display::showNumberHexEx(uint16_t num, uint8_t dots,
                                    bool leading_zero, uint8_t length,
                                    uint8_t pos) {
  showNumberBaseEx(16, num, dots, leading_zero, length, pos);
}

void TM1637Display::showNumberBaseEx(int8_t base, uint16_t num, uint8_t dots,
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

void TM1637Display::showDots(uint8_t dots, uint8_t* digits) {
  for (int i = 0; i < 4; ++i) {
    digits[i] |= (dots & 0x80);
    dots <<= 1;
  }
}

uint8_t TM1637Display::encodeDigit(uint8_t digit) {
  return digitToSegment[digit & 0x0f];
}
