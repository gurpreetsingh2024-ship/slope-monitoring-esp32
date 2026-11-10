// Phase 2 - Step 5: HW-390 capacitive soil moisture sensor calibration
// Wiring: VCC -> 3V3, GND -> GND, AOUT -> GPIO34 (D34, ADC1)
// Serial Monitor: 115200 baud
//
// Commands:
//   d = store current reading as DRY  (sensor held in open air)
//   w = store current reading as WET  (sensor in water, up to the printed line only)

const int MOIST_PIN = 34;

int dryRaw = -1;
int wetRaw = -1;

// Average 64 readings to smooth ADC noise
int readAvg() {
  long sum = 0;
  for (int i = 0; i < 64; i++) {
    sum += analogRead(MOIST_PIN);
    delay(2);
  }
  return (int)(sum / 64);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  analogReadResolution(12);                         // 0..4095
  analogSetPinAttenuation(MOIST_PIN, ADC_11db);     // full ~0-3.3 V range
  Serial.println("\nMoisture calibration. Send 'd' for DRY, 'w' for WET.\n");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'd' || c == 'D') {
      dryRaw = readAvg();
      Serial.printf(">>> DRY stored: %d\n", dryRaw);
    } else if (c == 'w' || c == 'W') {
      wetRaw = readAvg();
      Serial.printf(">>> WET stored: %d\n", wetRaw);
    }
  }

  int raw = readAvg();

  if (dryRaw > 0 && wetRaw > 0 && dryRaw != wetRaw) {
    float pct = 100.0f * (dryRaw - raw) / (float)(dryRaw - wetRaw);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    Serial.printf("raw = %4d   moisture = %5.1f %%   (dry=%d, wet=%d)\n", raw, pct, dryRaw, wetRaw);
  } else {
    Serial.printf("raw = %4d\n", raw);
  }

  delay(300);
}
