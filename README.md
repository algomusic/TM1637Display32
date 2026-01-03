This library supports the 7-segment displays driven by the TM1637 IC.

This version is non-blocking based on [TM1637-no-delay](https://github.com/cphouser/TM1637-no-delay) by cphouser which was a fork of a [library](https://github.com/avishorp/TM1637) by avishorp.

Updates in this version support faster 32 bit microcontrollers which needed adjusted timing between state changes to ensure the TM1637 processed the updates. Also the ESP32/RP2040 need explicit internal pull-ups. The library should still be backwards compatible for AVR microprocesses. There is extended alphanumeric and floating point number display support.

Inherited API:
  - setBrightness(brightness, on)
  - setSegments(segments[], length, pos)
  - clear()
  - showNumberDec(num, leading_zero, length, pos)
  - showNumberDecEx(num, dots, leading_zero, length, pos)
  - showNumberHexEx(num, dots, leading_zero, length, pos)
  - encodeDigit(digit)

New additions:
  - update() - non-blocking transmission
  - charToSeg(c) - character to segment
  - displayText(text, pos) - text display
  - displayCharAndNumber(c, number) - combined display
  - isIdle() - for use in main loop when using timer ISR for display update()
