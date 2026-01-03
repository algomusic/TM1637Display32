/*
 * TM1637Display32 ISR Example for ESP32
 *
 * Demonstrates the timer ISR approach for non-blocking display updates.
 * This method is recommended for real-time applications (audio, motor control)
 * where consistent timing is critical.
 *
 * Architecture:
 *   - Timer ISR calls display.update() every 100us for consistent bit-banging
 *   - Main loop uses display.isIdle() to check when ready for new content
 *   - No blocking, no race conditions
 *
 * Connections:
 *   CLK -> GPIO 18 (or your chosen pin)
 *   DIO -> GPIO 21 (or your chosen pin)
 *   VCC -> 3.3V or 5V
 *   GND -> GND
 */

#include <TM1637Display32.h>

// Pin definitions - adjust for your board
#define CLK 18
#define DIO 21

TM1637Display32 display(CLK, DIO);

// Hardware timer for display updates
hw_timer_t *displayTimer = NULL;

// Display state
bool displayUpdatePending = false;
char pendingChar = ' ';
int pendingValue = 0;

// Demo state
int counter = 0;
unsigned long lastUpdateTime = 0;

// Timer ISR - called every 100us to progress display state machine
void IRAM_ATTR onDisplayTimer() {
  display.update();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("TM1637 ISR Example");

  // Initialize display (before timer starts)
  display.setBrightness(3);
  display.clear();
  while (!display.update()) { delayMicroseconds(100); }  // Blocking OK before timer

  // Setup hardware timer for display updates (100us interval = 10kHz)
  displayTimer = timerBegin(1000000);  // 1MHz = 1us resolution
  timerAttachInterrupt(displayTimer, &onDisplayTimer);
  timerAlarm(displayTimer, 100, true, 0);  // 100us interval, auto-reload

  // Show startup message - timer now handles update() calls
  display.displayText("ISR");
  while (!display.isIdle()) { delayMicroseconds(100); }  // Wait for transmission
  delay(1000);

  Serial.println("Timer ISR started - display updates are now non-blocking");
}

void loop() {
  unsigned long now = millis();

  // Simulate some work (your real-time task would go here)
  // This represents audio processing, sensor reading, etc.
  delayMicroseconds(100);

  // Update display value periodically
  if (now - lastUpdateTime > 100) {  // Every 100ms
    lastUpdateTime = now;
    counter++;
    if (counter > 9999) counter = 0;

    // Queue display update - don't block!
    pendingChar = 'C';
    pendingValue = counter;
    displayUpdatePending = true;
  }

  // Start new display content only when idle (ISR handles the transmission)
  if (displayUpdatePending && display.isIdle()) {
    display.displayCharAndNumber(pendingChar, pendingValue);
    displayUpdatePending = false;
  }

  // Your other loop tasks go here...
  // They won't be blocked by display updates!
}
