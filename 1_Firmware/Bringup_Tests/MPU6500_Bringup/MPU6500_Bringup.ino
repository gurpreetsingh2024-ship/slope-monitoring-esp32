// Phase 2 - Steps 2-4: MPU-6500 bring-up
//   - Reads WHO_AM_I and tells you which chip you really have
//   - Streams accel (g), gyro (deg/s), temperature (C) at 10 Hz
//   - Send 'c' in the Serial Monitor to run an 8-second rest calibration
//
// Wiring: SDA -> GPIO21, SCL -> GPIO22, VCC -> 3V3, GND -> GND, AD0 -> GND
// Serial Monitor: 115200 baud
//
// Uses raw register access (no MPU6050 library), because many MPU6050
// libraries reject the MPU-6500's WHO_AM_I value (0x70).

#include <Wire.h>

const uint8_t MPU_ADDR = 0x68;          // change to 0x69 if the scanner found 0x69

const uint8_t REG_SMPLRT_DIV   = 0x19;
const uint8_t REG_CONFIG       = 0x1A;
const uint8_t REG_GYRO_CONFIG  = 0x1B;
const uint8_t REG_ACCEL_CONFIG = 0x1C;
const uint8_t REG_ACCEL_CFG2   = 0x1D;  // exists on MPU-6500 only
const uint8_t REG_ACCEL_XOUT_H = 0x3B;
const uint8_t REG_PWR_MGMT_1   = 0x6B;
const uint8_t REG_WHO_AM_I     = 0x75;

const float ACCEL_LSB_PER_G  = 16384.0f;  // +/-2 g range
const float GYRO_LSB_PER_DPS = 131.0f;    // +/-250 deg/s range

uint8_t chipId = 0;

struct Sample {
  float ax, ay, az;   // g
  float gx, gy, gz;   // deg/s
  float tempC;
};

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// Burst-read 14 bytes: accel XYZ, temp, gyro XYZ
bool readSample(Sample &s) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)14) != 14) return false;

  int16_t raw[7];
  for (int i = 0; i < 7; i++) {
    int hi = Wire.read();
    int lo = Wire.read();
    raw[i] = (int16_t)((hi << 8) | lo);
  }

  s.ax = raw[0] / ACCEL_LSB_PER_G;
  s.ay = raw[1] / ACCEL_LSB_PER_G;
  s.az = raw[2] / ACCEL_LSB_PER_G;

  // Temperature formula differs between the two chips
  if (chipId == 0x68) s.tempC = raw[3] / 340.0f + 36.53f;     // MPU-6050
  else                s.tempC = raw[3] / 333.87f + 21.0f;     // MPU-6500 / 9250

  s.gx = raw[4] / GYRO_LSB_PER_DPS;
  s.gy = raw[5] / GYRO_LSB_PER_DPS;
  s.gz = raw[6] / GYRO_LSB_PER_DPS;
  return true;
}

void runCalibration() {
  const int N = 1000;
  Serial.println("\n=== CALIBRATION: keep the board completely still (~8 s) ===");

  double sum[7] = {0}, sumSq[7] = {0};
  int n = 0;
  for (int i = 0; i < N; i++) {
    Sample s;
    if (readSample(s)) {
      float v[7] = {s.ax, s.ay, s.az, s.gx, s.gy, s.gz, s.tempC};
      for (int k = 0; k < 7; k++) {
        sum[k]   += v[k];
        sumSq[k] += (double)v[k] * v[k];
      }
      n++;
    }
    delay(8);
  }

  if (n < N / 2) {
    Serial.println("Too many failed reads - check wiring.");
    return;
  }

  const char* names[7] = {"ax (g)", "ay (g)", "az (g)", "gx (dps)", "gy (dps)", "gz (dps)", "temp (C)"};
  double mean[7];
  Serial.printf("Samples used: %d\n", n);
  Serial.println("channel      mean        std");
  for (int k = 0; k < 7; k++) {
    mean[k] = sum[k] / n;
    double var = sumSq[k] / n - mean[k] * mean[k];
    double sd = var > 0 ? sqrt(var) : 0;
    Serial.printf("%-9s %9.5f  %9.5f\n", names[k], mean[k], sd);
  }
  double mag = sqrt(mean[0]*mean[0] + mean[1]*mean[1] + mean[2]*mean[2]);
  Serial.printf("|a| of mean vector = %.4f g (should be ~1.00)\n", mag);
  Serial.println("=== WRITE THESE NUMBERS DOWN ===\n");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(21, 22);
  Wire.setClock(400000);

  chipId = readReg(REG_WHO_AM_I);
  Serial.printf("\nWHO_AM_I = 0x%02X\n", chipId);

  if (chipId == 0x70)      Serial.println("-> MPU-6500 confirmed");
  else if (chipId == 0x68) Serial.println("-> This chip is an MPU-6050, not a 6500");
  else if (chipId == 0x71 || chipId == 0x73) Serial.println("-> Looks like an MPU-9250 family chip");
  else if (chipId == 0xFF) {
    Serial.println("-> No response. Check wiring / address (0x68 vs 0x69).");
    while (true) delay(1000);
  } else {
    Serial.println("-> Unrecognised ID (clone?). Continuing with 6500 settings.");
  }

  writeReg(REG_PWR_MGMT_1, 0x80);   // reset
  delay(100);
  writeReg(REG_PWR_MGMT_1, 0x01);   // wake up, use gyro PLL clock
  delay(100);

  writeReg(REG_SMPLRT_DIV, 7);      // 1 kHz / (1+7) = 125 Hz internal rate
  writeReg(REG_CONFIG, 0x03);       // gyro/temp low-pass ~41 Hz
  writeReg(REG_GYRO_CONFIG, 0x00);  // +/-250 deg/s
  writeReg(REG_ACCEL_CONFIG, 0x00); // +/-2 g
  if (chipId != 0x68) {
    writeReg(REG_ACCEL_CFG2, 0x03); // accel low-pass ~45 Hz (6500 only)
  }

  Serial.println("Streaming. Send 'c' to calibrate.\n");
  Serial.println("ax(g)   ay(g)   az(g)   |a|(g)  roll(deg) pitch(deg) | gx gy gz (dps) | temp(C)");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c' || c == 'C') runCalibration();
  }

  static uint32_t last = 0;
  if (millis() - last < 100) return;   // 10 Hz print rate
  last = millis();

  Sample s;
  if (!readSample(s)) {
    Serial.println("I2C read failed");
    return;
  }

  float mag   = sqrtf(s.ax*s.ax + s.ay*s.ay + s.az*s.az);
  float roll  = atan2f(s.ay, s.az) * 57.29578f;
  float pitch = atan2f(-s.ax, sqrtf(s.ay*s.ay + s.az*s.az)) * 57.29578f;

  Serial.printf("%7.3f %7.3f %7.3f %7.3f %8.2f %9.2f | %6.2f %6.2f %6.2f | %5.1f\n",
                s.ax, s.ay, s.az, mag, roll, pitch, s.gx, s.gy, s.gz, s.tempC);
}
