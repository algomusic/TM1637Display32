/*
 * TM1637Display32 Library Test
 *
 * Demonstrates the non-blocking TM1637 display library features:
 * - Text display (displayText)
 * - Character + number display (displayCharAndNumber)
 * - Number display (showNumberDec)
 * - Animated value changes simulating potentiometer movement
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

// Animation state
int currentValue = 0;
int targetValue = 0;
int direction = 1;
unsigned long lastUpdateTime = 0;
unsigned long lastAnimationTime = 0;
int testPhase = 0;
unsigned long phaseStartTime = 0;

// Test parameters
const char* testLabels[] = {"V", "F", "A", "r", "b", "T", "D", "O"};
const int numLabels = 8;
int currentLabel = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  display.setBrightness(3);  // 0-7, moderate brightness
  display.clear();
  while (!display.update()) { delayMicroseconds(100); }

  Serial.println("TM1637Display32 Test Starting...");
  phaseStartTime = millis();
}

void loop() {
  unsigned long now = millis();

  // Run different test phases
  switch (testPhase) {
    case 0: testTextDisplay(now); break;
    case 1: testCharAndNumber(now); break;
    case 2: testPotSimulation(now); break;
    case 3: testCountUp(now); break;
    case 4: testCountDown(now); break;
    case 5: testLargeNumbers(now); break;
    default: testPhase = 0; phaseStartTime = now; break;
  }

  // Non-blocking display update - call frequently from loop()
  // For real-time audio apps: use a timer ISR for update() and isIdle() in loop
  display.update();
}

// Phase 0: Cycle through text messages
void testTextDisplay(unsigned long now) {
  static int textIndex = 0;
  static unsigned long lastChange = 0;

  const char* messages[] = {"HELO", "tESt", "PLAY", "StOP", "SAVE", "LOAd", "Edit", "done"};
  const int numMessages = 8;

  if (now - lastChange > 800) {
    lastChange = now;
    display.displayText(messages[textIndex]);
    Serial.print("Text: ");
    Serial.println(messages[textIndex]);
    textIndex = (textIndex + 1) % numMessages;
  }

  // Move to next phase after showing all messages twice
  if (now - phaseStartTime > 6500) {
    testPhase = 1;
    phaseStartTime = now;
    currentLabel = 0;
    currentValue = 0;
    Serial.println("\n--- Phase 1: Char + Number Display ---");
  }
}

// Phase 1: Show different parameter labels with values
void testCharAndNumber(unsigned long now) {
  static unsigned long lastChange = 0;

  if (now - lastChange > 600) {
    lastChange = now;
    display.displayCharAndNumber(testLabels[currentLabel][0], currentValue);
    Serial.print(testLabels[currentLabel]);
    Serial.print(": ");
    Serial.println(currentValue);

    currentValue += 50;
    if (currentValue > 500) {
      currentValue = 0;
      currentLabel = (currentLabel + 1) % numLabels;
    }
  }

  if (now - phaseStartTime > 8000) {
    testPhase = 2;
    phaseStartTime = now;
    currentValue = 500;
    targetValue = 500;
    Serial.println("\n--- Phase 2: Pot Simulation ---");
  }
}

// Phase 2: Simulate potentiometer movement (smooth ramping)
void testPotSimulation(unsigned long now) {
  static unsigned long lastTargetChange = 0;
  static unsigned long lastValueUpdate = 0;

  // Change target periodically (simulates user turning pot)
  if (now - lastTargetChange > 1500) {
    lastTargetChange = now;
    targetValue = random(0, 1000);
    Serial.print("New target: ");
    Serial.println(targetValue);
  }

  // Smoothly move current value toward target
  if (now - lastValueUpdate > 30) {
    lastValueUpdate = now;
    if (currentValue < targetValue) {
      currentValue += max(1, (targetValue - currentValue) / 10);
    } else if (currentValue > targetValue) {
      currentValue -= max(1, (currentValue - targetValue) / 10);
    }
    display.displayCharAndNumber('P', currentValue);
  }

  if (now - phaseStartTime > 8000) {
    testPhase = 3;
    phaseStartTime = now;
    currentValue = 0;
    Serial.println("\n--- Phase 3: Count Up ---");
  }
}

// Phase 3: Count up animation
void testCountUp(unsigned long now) {
  static unsigned long lastChange = 0;

  if (now - lastChange > 50) {
    lastChange = now;
    display.showNumberDec(currentValue, false, 4, 0);
    currentValue += 7;
    if (currentValue > 9999) currentValue = 0;
  }

  if (now - phaseStartTime > 5000) {
    testPhase = 4;
    phaseStartTime = now;
    currentValue = 999;
    Serial.println("\n--- Phase 4: Count Down ---");
  }
}

// Phase 4: Count down animation
void testCountDown(unsigned long now) {
  static unsigned long lastChange = 0;

  if (now - lastChange > 80) {
    lastChange = now;
    display.showNumberDec(currentValue, false, 4, 0);
    currentValue -= 3;
    if (currentValue < 0) currentValue = 999;
  }

  if (now - phaseStartTime > 5000) {
    testPhase = 5;
    phaseStartTime = now;
    currentValue = 0;
    Serial.println("\n--- Phase 5: Large Numbers (with decimal) ---");
  }
}

// Phase 5: Test large number display with decimal points
void testLargeNumbers(unsigned long now) {
  static unsigned long lastChange = 0;

  if (now - lastChange > 100) {
    lastChange = now;
    // displayCharAndNumber handles large numbers with decimal formatting
    display.displayCharAndNumber('F', currentValue);
    currentValue += 137;
    if (currentValue > 20000) currentValue = 0;
  }

  if (now - phaseStartTime > 6000) {
    testPhase = 0;
    phaseStartTime = now;
    Serial.println("\n--- Restarting test cycle ---\n");
    Serial.println("--- Phase 0: Text Display ---");
  }
}
