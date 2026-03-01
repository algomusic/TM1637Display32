//  TM1637Display32 - Non-blocking version with timing support
//
//  Original Author: avishorp@gmail.com
//  Non-blocking fork: cphouser
//  ESP32/RP2040 timing fixes: algomusic 2025
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.

#ifndef __TM1637DISPLAY32__
#define __TM1637DISPLAY32__

#include <inttypes.h>

#define SEG_A   0b00000001
#define SEG_B   0b00000010
#define SEG_C   0b00000100
#define SEG_D   0b00001000
#define SEG_E   0b00010000
#define SEG_F   0b00100000
#define SEG_G   0b01000000
#define SEG_DP  0b10000000

class TM1637Display32 {
public:
  //! Initialize a TM1637Display object
  //! @param pinClk - Digital pin connected to CLK
  //! @param pinDIO - Digital pin connected to DIO
  TM1637Display32(uint8_t pinClk, uint8_t pinDIO);

  //! Non-blocking update - call from timer ISR for consistent timing
  //! Progresses the display transmission one step at a time.
  //! @return true if no transmission in progress (idle), false if busy
  //! WARNING: Do not call from both ISR and main loop - use isIdle() in main loop
  bool update();

  //! Check if display is idle (no transmission in progress)
  //! Safe to call from main loop while ISR handles update()
  //! @return true if idle, false if busy
  bool isIdle() const;

  //! Pump the state machine until transmission completes or timeout.
  //! Use this in loop()-based (non-ISR) projects to drive the display.
  //! Respects internal BIT_DELAY rate limiting between steps.
  //! @param timeout_us Maximum time to pump in microseconds (default 25000 = 25ms)
  //! @return true if idle (complete or no transmission), false if timed out
  bool pump(unsigned long timeout_us = 25000);

  //! Set minimum interval between display transmissions (for polled mode).
  //! Prevents rapid updates (e.g., from turning a dial) from blocking the main loop.
  //! @param interval_ms Minimum milliseconds between transmissions (0 = no throttle)
  void setMinInterval(unsigned long interval_ms);

  //! Check if the display is ready to accept new content.
  //! Returns true only if idle AND the minimum interval has elapsed since
  //! the last transmission started. Use instead of isIdle() when throttling.
  //! @return true if ready for new content
  bool isReadyForUpdate();

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

  //! Convert a character (A-Z, a-z, 0-9, space, dash) to segment pattern
  uint8_t charToSeg(char c);

  //! Display up to 4 characters starting at position (0-3)
  void displayText(const char* text, uint8_t pos = 0);

  //! Display a character at position 0 and a number at positions 1-3
  //! Numbers >= 1000 show with decimal point (e.g., 1230 -> "1.23")
  //! Numbers >= 10000 show as XX.X (e.g., 12300 -> "12.3")
  void displayCharAndNumber(char c, int number);

  //! Start scrolling text across the display
  //! @param text The text to scroll (will be padded with spaces)
  //! @param interval_ms Milliseconds between scroll steps (default 300)
  //! @param pad_spaces Spaces to add at start and end for smooth scroll (default 4)
  void startScroll(const char* text, uint16_t interval_ms = 300, uint8_t pad_spaces = 4);

  //! Update scrolling - call from loop()
  //! @return true when scrolling is complete, false if still scrolling
  bool updateScroll();

  //! Check if currently scrolling
  bool isScrolling() const;

  //! Stop scrolling immediately
  void stopScroll();

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
  // volatile: these are modified by ISR and read by main loop
  volatile uint8_t m_counter;        // Step within current phase
  volatile uint8_t m_phase;          // Current protocol phase (0-8)
  volatile uint8_t m_byte;           // Current byte being transmitted
  volatile uint8_t m_bit_count;      // Bits transmitted of current byte
  volatile uint8_t m_currentSegment; // Current segment being transmitted

  // Timing for rate limiting and watchdog
  unsigned long m_lastUpdateMicros;
  unsigned long m_transmissionStartMillis;  // For timeout detection
  unsigned long m_lastTransmissionMillis;   // For update throttling
  unsigned long m_minIntervalMillis;        // Minimum ms between transmissions (0 = no throttle)

  // Internal protocol helpers
  bool writeBit();          // Write one bit, returns true when byte complete
  bool startCondition();    // Generate start, returns true when complete
  bool stopCondition();     // Generate stop, returns true when complete

  // Scrolling state
  char m_scrollBuffer[32];        // Buffer for padded scroll text (max 24 chars + padding)
  uint8_t m_scrollLength;         // Length of text in buffer
  uint8_t m_scrollPos;            // Current scroll position
  uint16_t m_scrollInterval;      // Ms between scroll steps
  unsigned long m_scrollLastStep; // Time of last scroll step
  bool m_scrollActive;            // Whether scrolling is active
};

#endif // __TM1637DISPLAY32__
