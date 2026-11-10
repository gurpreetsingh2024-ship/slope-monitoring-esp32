// Phase 2 - Step 1: I2C scanner
// Wiring: MPU SDA -> GPIO21 (D21), MPU SCL -> GPIO22 (D22)
// Serial Monitor: 115200 baud

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(21, 22);        // SDA, SCL
  Wire.setClock(400000);     // 400 kHz fast mode
  Serial.println("\nI2C scanner started...");
}

void loop() {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Found device at 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("No I2C devices found - check wiring");
  }
  Serial.println("---");
  delay(3000);
}
