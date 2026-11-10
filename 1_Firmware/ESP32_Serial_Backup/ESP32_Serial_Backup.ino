// Phase 3: windowed sampling + feature extraction
// MPU-6500 (I2C: SDA = GPIO21, SCL = GPIO22) + HW-390 moisture (AOUT = GPIO34)
//
// One window = 512 samples at 128 Hz (4 s). For every window the sketch
// prints ONE CSV line that starts with "FEAT,".
//
// Serial Monitor: 115200 baud. Command:
//   r = also dump the raw accel samples of the NEXT window (for the numpy check)

#include <Wire.h>
#include <math.h>
#include "esp_timer.h"

// ===================== USER SETTINGS (edit after Step 1) =====================
// Final mounting: board vertical, pins DOWN, chip face OUTWARD.
// Set from the Step 1 test: upright ax = +1.056 g; az went negative on an outward lean.
#define UP_AXIS   0     // sensor axis that runs UP the wall: 0 = X, 1 = Y
#define UP_SIGN   1     // +1 if that axis reads about +1 g when mounted, -1 if about -1 g
#define OUT_SIGN  1     // flip to -1 if leaning the wall top OUTWARD gives a NEGATIVE tilt_out

const float MOIST_DRY = 3115.0f;   // from Phase 2 (redo in sand during Phase 4)
const float MOIST_WET = 1258.0f;
// ============================================================================

#if (UP_AXIS != 0) && (UP_AXIS != 1)
#error "UP_AXIS must be 0 (X) or 1 (Y)"
#endif
#define LAT_AXIS (1 - UP_AXIS)

// MPU registers
const uint8_t MPU_ADDR         = 0x68;
const uint8_t REG_SMPLRT_DIV   = 0x19;
const uint8_t REG_CONFIG       = 0x1A;
const uint8_t REG_GYRO_CONFIG  = 0x1B;
const uint8_t REG_ACCEL_CONFIG = 0x1C;
const uint8_t REG_ACCEL_CFG2   = 0x1D;   // MPU-6500 only
const uint8_t REG_ACCEL_XOUT_H = 0x3B;
const uint8_t REG_PWR_MGMT_1   = 0x6B;
const uint8_t REG_WHO_AM_I     = 0x75;

const float ACCEL_LSB_PER_G = 16384.0f;  // +/-2 g

// Windowing
const int    FS_HZ     = 128;
const int    N         = 512;                 // power of two (512 samples = 4 s)
const double PERIOD_US = 1000000.0 / FS_HZ;   // 7812.5 us
const float  F_MIN_HZ  = 0.5f;                // ignore DC and very slow drift
const float  RAD2DEG   = 57.29578f;
const int    MOIST_PIN = 34;

uint8_t  chipId = 0;
bool     dumpNext = false;
uint32_t winCount = 0;

// ===== DSP BEGIN (this block is also compiled and tested on a PC) =====
float buf[3][N];                 // accel samples of the current window, in g
float re[N], im[N], psd[N / 2];
float twCos[N / 2], twSin[N / 2], hann[N];

struct Feat {
  float mean[3];     // mean accel per axis (g)
  float upG;         // mean accel along the "up" axis (about +1 g when mounted correctly)
  float tiltOut;     // outward lean, degrees (+ = top leans outward)
  float tiltLat;     // sideways lean, degrees
  float vibMg;       // RMS of the dynamic acceleration, milli-g
  float domHz;       // dominant frequency, Hz
  float domRatio;    // peak power / total power in band (0..1): tone vs broadband
};

void initTables() {
  for (int k = 0; k < N / 2; k++) {
    twCos[k] = (float)cos(2.0 * M_PI * k / N);
    twSin[k] = (float)(-sin(2.0 * M_PI * k / N));
  }
  for (int i = 0; i < N; i++) {
    hann[i] = (float)(0.5 - 0.5 * cos(2.0 * M_PI * i / (N - 1)));
  }
}

// In-place radix-2 FFT on re[] / im[] (length N)
void fft() {
  for (int i = 1, j = 0; i < N; i++) {
    int bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= N; len <<= 1) {
    int half = len >> 1;
    int step = N / len;
    for (int i = 0; i < N; i += len) {
      for (int k = 0; k < half; k++) {
        float wr = twCos[k * step];
        float wi = twSin[k * step];
        int u = i + k, v = i + k + half;
        float xr = re[v] * wr - im[v] * wi;
        float xi = re[v] * wi + im[v] * wr;
        re[v] = re[u] - xr;  im[v] = im[u] - xi;
        re[u] += xr;         im[u] += xi;
      }
    }
  }
}

Feat computeFeatures() {
  Feat f;
  float var[3];

  for (int a = 0; a < 3; a++) {
    double s = 0;
    for (int i = 0; i < N; i++) s += buf[a][i];
    f.mean[a] = (float)(s / N);
    double v = 0;
    for (int i = 0; i < N; i++) {
      double d = buf[a][i] - f.mean[a];
      v += d * d;
    }
    var[a] = (float)(v / N);
  }
  f.vibMg = 1000.0f * sqrtf(var[0] + var[1] + var[2]);

  // Tilt from the mean gravity vector
  float up  = UP_SIGN * f.mean[UP_AXIS];
  float lat = f.mean[LAT_AXIS];
  float out = OUT_SIGN * f.mean[2];          // +Z points out of the chip face
  f.upG     = up;
  f.tiltOut = atan2f(-out, up) * RAD2DEG;
  f.tiltLat = atan2f(lat, up) * RAD2DEG;

  // Power spectrum summed over the three axes
  for (int k = 0; k < N / 2; k++) psd[k] = 0;
  for (int a = 0; a < 3; a++) {
    for (int i = 0; i < N; i++) {
      re[i] = (buf[a][i] - f.mean[a]) * hann[i];
      im[i] = 0;
    }
    fft();
    for (int k = 1; k < N / 2; k++) psd[k] += re[k] * re[k] + im[k] * im[k];
  }

  int kMin = (int)ceilf(F_MIN_HZ * N / FS_HZ);
  int kPk = kMin;
  double tot = 0;
  for (int k = kMin; k < N / 2; k++) {
    tot += psd[k];
    if (psd[k] > psd[kPk]) kPk = k;
  }
  f.domHz    = (float)kPk * FS_HZ / N;
  f.domRatio = tot > 0 ? (float)(psd[kPk] / tot) : 0.0f;
  return f;
}
// ===== DSP END =====

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

// Accel XYZ (g) + die temperature (C), one burst of 8 bytes
bool readAccelTemp(float a[3], float &tempC) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)8) != 8) return false;
  int16_t raw[4];
  for (int i = 0; i < 4; i++) {
    int hi = Wire.read();
    int lo = Wire.read();
    raw[i] = (int16_t)((hi << 8) | lo);
  }
  a[0] = raw[0] / ACCEL_LSB_PER_G;
  a[1] = raw[1] / ACCEL_LSB_PER_G;
  a[2] = raw[2] / ACCEL_LSB_PER_G;
  tempC = (chipId == 0x68) ? raw[3] / 340.0f + 36.53f : raw[3] / 333.87f + 21.0f;
  return true;
}

static inline void waitUntil(int64_t tUs) {
  int64_t now = esp_timer_get_time();
  if (tUs - now > 2000) delay((uint32_t)((tUs - now - 1000) / 1000));
  while (esp_timer_get_time() < tUs) { }
}

struct WinStats {
  int    late;       // samples taken more than 1 ms after their slot
  int    bad;        // failed I2C reads
  double durS;       // time from first to last sample (ideal: 511/128 = 3.992 s)
  float  tempMean;
  float  moistMean;  // raw ADC counts
};

WinStats collectWindow() {
  WinStats w = {0, 0, 0.0, 0.0f, 0.0f};
  double tSum = 0, mSum = 0;
  float last[3] = {0, 0, 1};
  float lastT = 25.0f;
  int64_t tFirst = 0, tLast = 0;
  double tNext = (double)esp_timer_get_time() + PERIOD_US;

  for (int i = 0; i < N; i++) {
    waitUntil((int64_t)tNext);
    int64_t now = esp_timer_get_time();
    if (now - (int64_t)tNext > 1000) w.late++;
    if (i == 0) tFirst = now;
    tLast = now;

    float a[3], t;
    if (readAccelTemp(a, t)) {
      last[0] = a[0]; last[1] = a[1]; last[2] = a[2]; lastT = t;
    } else {
      w.bad++;                       // keep the previous sample
    }
    buf[0][i] = last[0];
    buf[1][i] = last[1];
    buf[2][i] = last[2];
    tSum += lastT;
    mSum += analogRead(MOIST_PIN);
    tNext += PERIOD_US;
  }
  w.durS      = (double)(tLast - tFirst) / 1e6;
  w.tempMean  = (float)(tSum / N);
  w.moistMean = (float)(mSum / N);
  return w;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(21, 22);
  Wire.setClock(400000);

  chipId = readReg(REG_WHO_AM_I);
  Serial.printf("\n# WHO_AM_I = 0x%02X\n", chipId);
  if (chipId == 0xFF) {
    Serial.println("# No MPU response - check wiring");
    while (true) delay(1000);
  }

  writeReg(REG_PWR_MGMT_1, 0x80);    // reset
  delay(100);
  writeReg(REG_PWR_MGMT_1, 0x01);    // wake, gyro PLL clock
  delay(100);
  writeReg(REG_SMPLRT_DIV, 0);       // 1 kHz internal rate, we poll at 128 Hz
  writeReg(REG_CONFIG, 0x03);        // low-pass ~41 Hz
  writeReg(REG_GYRO_CONFIG, 0x00);
  writeReg(REG_ACCEL_CONFIG, 0x00);  // +/-2 g
  if (chipId != 0x68) writeReg(REG_ACCEL_CFG2, 0x03);   // accel low-pass ~45 Hz (6500)

  analogReadResolution(12);
  analogSetPinAttenuation(MOIST_PIN, ADC_11db);

  initTables();

  Serial.printf("# MAPPING UP_AXIS=%d UP_SIGN=%d OUT_SIGN=%d\n", UP_AXIS, UP_SIGN, OUT_SIGN);
  Serial.println("# COLUMNS win,t_ms,tilt_out_deg,tilt_lat_deg,ax_g,ay_g,az_g,vib_rms_mg,"
                 "dom_hz,dom_ratio,temp_c,moist_raw,moist_pct,late,bad,dur_s,proc_ms");
  Serial.println("# Collecting first window (about 8 s)...");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      dumpNext = true;
      Serial.println("# raw dump armed for the next window");
    }
  }

  WinStats w = collectWindow();

  uint32_t tp0 = millis();
  Feat f = computeFeatures();
  uint32_t procMs = millis() - tp0;

  float moistPct = 100.0f * (MOIST_DRY - w.moistMean) / (MOIST_DRY - MOIST_WET);
  if (moistPct < 0) moistPct = 0;
  if (moistPct > 100) moistPct = 100;

  if (dumpNext) {
    for (int i = 0; i < N; i++) {
      Serial.printf("RAW,%d,%.6f,%.6f,%.6f\n", i, buf[0][i], buf[1][i], buf[2][i]);
    }
    dumpNext = false;
  }

  winCount++;
  Serial.printf("FEAT,%lu,%lu,%.3f,%.3f,%.5f,%.5f,%.5f,%.3f,%.3f,%.4f,%.2f,%.1f,%.1f,%d,%d,%.3f,%lu\n",
                (unsigned long)winCount, (unsigned long)millis(),
                f.tiltOut, f.tiltLat, f.mean[0], f.mean[1], f.mean[2],
                f.vibMg, f.domHz, f.domRatio, w.tempMean, w.moistMean, moistPct,
                w.late, w.bad, w.durS, (unsigned long)procMs);

  if (f.upG < 0.5f) {
    Serial.println("# WARNING: 'up' axis reads below 0.5 g - check UP_AXIS / UP_SIGN against the Step 1 test");
  }
}
