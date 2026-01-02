//  TM1637Display - Non-blocking version with timing support
//
//  Original Author: avishorp@gmail.com
//  Non-blocking fork: cphouser
//  ESP32/RP2040 timing fixes: 2025
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.

#ifndef __TM1637DISPLAY__
#define __TM1637DISPLAY__

#include <inttypes.h>

#define SEG_A   0b00000001
#define SEG_B   0b00000010
#define SEG_C   0b00000100
#define SEG_D   0b00001000
#define SEG_E   0b00010000
#define SEG_F   0b00100000
#define SEG_G   0b01000000
#define SEG_DP  0b10000000

class TM1637Display {
public:
  //! Initialize a TM1637Display object
  //! @param pinClk - Digital pin connected to CLK
  //! @param pinDIO - Digital pin connected to DIO
  TM1637Display(uint8_t pinClk, uint8_t pinDIO);

  //! Non-blocking update - call this frequently from loop()
  //! Progresses the display transmission one step at a time.
  //! @return true if no transmission in progress (idle), false if busy
  bool update();

  //! Sets the brightness (takes effect on next setSegments call)
  //! @param brightness 0-7 (lowest to highest)
  //! @param on Turn display on or off
  void setBrightness(uint8_t brightness, bool on = true);

  //! Display raw segment data
  //! @param segments Array of segment values
  //! @param length Number of digits (1-4)
  //! @param pos Starting position (0-3)
  void setSegments(const uint8_t segments[], uint8_t length = 4, uint8_t pos = 0);

  //! Clear the display
  void clear();

  //! Display a decimal number
  void showNumberDec(int num, bool leading_zero = false, uint8_t length = 4, uint8_t pos = 0);

  //! Display a decimal number with dot control
  void showNumberDecEx(int num, uint8_t dots = 0, bool leading_zero = false,
                       uint8_t length = 4, uint8_t pos = 0);

  //! Display a hexadecimal number with dot control
  void showNumberHexEx(uint16_t num, uint8_t dots = 0, bool leading_zero = false,
                       uint8_t length = 4, uint8_t pos = 0);

  //! Encode a single digit (0-15) to segment pattern
  uint8_t encodeDigit(uint8_t digit);

protected:
  void showDots(uint8_t dots, uint8_t* digits);
  void showNumberBaseEx(int8_t base, uint16_t num, uint8_t dots = 0,
                        bool leading_zero = false, uint8_t length = 4, uint8_t pos = 0);

private:
  // Pin configuration
  uint8_t m_pinClk;
  uint8_t m_pinDIO;

  // Display settings
  uint8_t m_brightness;
  uint8_t m_segments[4];
  uint8_t m_length;
  uint8_t m_pos;

  // State machine for non-blocking transmission
  uint8_t m_counter;        // Step within current phase
  uint8_t m_phase;          // Current protocol phase (0-8)
  uint8_t m_byte;           // Current byte being transmitted
  uint8_t m_bit_count;      // Bits transmitted of current byte
  uint8_t m_currentSegment; // Current segment being transmitted

  // Timing for rate limiting
  unsigned long m_lastUpdateMicros;

  // Internal protocol helpers
  bool writeBit();          // Write one bit, returns true when byte complete
  bool startCondition();    // Generate start, returns true when complete
  bool stopCondition();     // Generate stop, returns true when complete
};

#endif // __TM1637DISPLAY__
