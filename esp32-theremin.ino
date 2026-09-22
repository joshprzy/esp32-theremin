// Quick ESP32 theremin
// Pitch antenna  -> GPIO4
// Volume antenna -> GPIO13
// Audio out      -> GPIO25 (DAC on classic ESP32 / S2)
//                -> GPIO17 PWM on S3 (your board)
//
// Serial 115200. Hands away ~1.5 s to calibrate. Type c to recalibrate.
// Line-level audio only. Use 1k + 10 uF into an aux cable.

#include <Arduino.h>
#include "soc/soc_caps.h"
#include <math.h>

static const int PIN_PITCH  = 4;
static const int PIN_VOLUME = 13;

#if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
  static const int PIN_AUDIO = 25;
#else
  static const int PIN_AUDIO = 17;
#endif

#if defined(CONFIG_IDF_TARGET_ESP32)
  static const bool TOUCH_FALLS_WHEN_NEAR = true;
#else
  static const bool TOUCH_FALLS_WHEN_NEAR = false;
#endif

static const float FREQ_MIN_HZ   = 110.0f;
static const float FREQ_MAX_HZ   = 880.0f;
static const float SAMPLE_HZ     = 16000.0f;
static const uint32_t SAMPLE_US  = (uint32_t)(1000000.0f / SAMPLE_HZ);
static const int SINE_BITS       = 8;
static const int SINE_LEN        = 1 << SINE_BITS;

static uint8_t sine[SINE_LEN];
static float pitchBase = 0, volBase = 0, pitchSpan = 1, volSpan = 1;
static volatile float freqHz = 0, amp = 0;
static uint32_t phase = 0, phaseInc = 0, nextSampleUs = 0;

static float mix(float prev, float sample, float alpha) {
  return prev + alpha * (sample - prev);
}

static float touchNow(int pin) {
  uint32_t acc = 0;
  for (int i = 0; i < 4; i++) acc += touchRead(pin);
  return acc * 0.25f;
}

static float proximity(float raw, float base, float span) {
  float p = TOUCH_FALLS_WHEN_NEAR ? (base - raw) / span : (raw - base) / span;
  if (p < 0) p = 0;
  if (p > 1) p = 1;
  return p * p;
}

static void calibrate() {
  Serial.println("Calibrating. Hands away from both antennas...");
  delay(400);
  float pSum = 0, vSum = 0;
  const int N = 40;
  for (int i = 0; i < N; i++) {
    pSum += touchNow(PIN_PITCH);
    vSum += touchNow(PIN_VOLUME);
    delay(25);
  }
  pitchBase = pSum / N;
  volBase   = vSum / N;
  if (TOUCH_FALLS_WHEN_NEAR) {
    pitchSpan = max(12.0f, pitchBase * 0.45f);
    volSpan   = max(12.0f, volBase * 0.45f);
  } else {
    pitchSpan = max(200.0f, pitchBase * 0.8f);
    volSpan   = max(200.0f, volBase * 0.8f);
  }
  Serial.printf("pitch base=%.1f span=%.1f\n", pitchBase, pitchSpan);
  Serial.printf("volume base=%.1f span=%.1f\n", volBase, volSpan);
  Serial.println("Play.");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("ESP32 theremin");
  Serial.printf("pitch=GPIO%d  volume=GPIO%d  audio=GPIO%d (%s)\n",
                PIN_PITCH, PIN_VOLUME, PIN_AUDIO,
#if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
                "DAC");
#else
                "PWM");
#endif

  for (int i = 0; i < SINE_LEN; i++) {
    float th = (2.0f * PI * i) / SINE_LEN;
    sine[i] = (uint8_t)lroundf(127.5f + 127.0f * sinf(th));
  }

#if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
  dacWrite(PIN_AUDIO, 128);
#else
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(PIN_AUDIO, 22050, 8);
  ledcWrite(PIN_AUDIO, 128);
#else
  ledcSetup(0, 22050, 8);
  ledcAttachPin(PIN_AUDIO, 0);
  ledcWrite(0, 128);
#endif
#endif

  calibrate();
  nextSampleUs = micros();
}

void loop() {
  static uint32_t lastSenseMs = 0;
  static float pSmooth = 0, vSmooth = 0;
  uint32_t nowMs = millis();

  if (nowMs - lastSenseMs >= 12) {
    lastSenseMs = nowMs;
    float p = proximity(touchNow(PIN_PITCH), pitchBase, pitchSpan);
    float v = proximity(touchNow(PIN_VOLUME), volBase, volSpan);
    pSmooth = mix(pSmooth, p, 0.25f);
    vSmooth = mix(vSmooth, v, 0.25f);

    if (pSmooth < 0.04f) {
      freqHz = 0;
      amp = mix(amp, 0, 0.4f);
    } else {
      freqHz = FREQ_MIN_HZ * powf(FREQ_MAX_HZ / FREQ_MIN_HZ, pSmooth);
      amp = mix(amp, max(0.12f, vSmooth), 0.3f);
    }

    phaseInc = (uint32_t)((freqHz * (float)SINE_LEN / SAMPLE_HZ) * 65536.0f);

    static uint32_t lastPrint = 0;
    if (nowMs - lastPrint >= 200) {
      lastPrint = nowMs;
      Serial.printf("p=%.2f v=%.2f  %6.1f Hz  rawP=%.0f rawV=%.0f\n",
                    pSmooth, vSmooth, freqHz,
                    touchNow(PIN_PITCH), touchNow(PIN_VOLUME));
    }
  }

  if (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'c' || c == 'C') calibrate();
  }

  uint32_t nowUs = micros();
  int guard = 0;
  while ((int32_t)(nowUs - nextSampleUs) >= 0 && guard++ < 16) {
    nextSampleUs += SAMPLE_US;
    phase += phaseInc;
    uint8_t idx = (uint8_t)((phase >> 16) & (SINE_LEN - 1));
    int out = 128 + (int)(((int)sine[idx] - 128) * amp);
    if (out < 0) out = 0;
    if (out > 255) out = 255;
#if defined(SOC_DAC_SUPPORTED) && SOC_DAC_SUPPORTED
    dacWrite(PIN_AUDIO, (uint8_t)out);
#else
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(PIN_AUDIO, (uint8_t)out);
#else
    ledcWrite(0, (uint8_t)out);
#endif
#endif
    nowUs = micros();
  }
}
