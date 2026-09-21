// Quick ESP32 theremin
// Pitch antenna  -> GPIO4  (T0 on classic ESP32, TOUCH1 on S2/S3)
// Volume antenna -> GPIO13 (T4 on classic ESP32, TOUCH13 on S2/S3)
// Audio out      -> GPIO25 (DAC1 on classic ESP32 / S2)
//                -> GPIO17 PWM fallback on chips with no DAC (S3, C3, C6)
//
// Open Serial Monitor at 115200 after boot. Keep hands away for ~1.5 s
// while it samples the untouched baseline. Then move a hand toward the
// pitch wire. Closer = higher note. Second wire is volume.
//
// Audio is a weak line-level signal. Do not drive a speaker directly.
// Use an LM386, PAM8403, powered speakers, or headphones through a
// 1k series resistor + 10 uF coupling cap.

#include <Arduino.h>
#include <math.h>

static const int PIN_PITCH  = 4;
static const int PIN_VOLUME = 13;

#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
  static const int PIN_AUDIO = 25;   // DAC
  static const bool USE_DAC  = true;
#else
  static const int PIN_AUDIO = 17;   // PWM / LEDC
  static const bool USE_DAC  = false;
#endif

#if defined(CONFIG_IDF_TARGET_ESP32)
  // Original ESP32: smaller touchRead() means closer
  static const bool TOUCH_FALLS_WHEN_NEAR = true;
#else
  // S2 / S3 (and software-style ports): larger reading means closer
  static const bool TOUCH_FALLS_WHEN_NEAR = false;
#endif

static const float FREQ_MIN_HZ   = 110.0f;   // A2
static const float FREQ_MAX_HZ   = 880.0f;   // A5
static const float SAMPLE_HZ     = 16000.0f;
static const uint32_t SAMPLE_US  = (uint32_t)(1000000.0f / SAMPLE_HZ);
static const int SINE_BITS       = 8;
static const int SINE_LEN        = 1 << SINE_BITS;

static uint8_t sine[SINE_LEN];

static float pitchBase = 0;
static float volBase   = 0;
static float pitchSpan = 1;
static float volSpan   = 1;

static volatile float freqHz = 0;
static volatile float amp    = 0;

static uint32_t phase = 0;
static uint32_t phaseInc = 0;
static uint32_t nextSampleUs = 0;

static float ema(float prev, float sample, float alpha) {
  return prev + alpha * (sample - prev);
}

static float touchNow(int pin) {
  // Average a few raw reads; the pad is noisy at distance.
  uint32_t acc = 0;
  for (int i = 0; i < 4; i++) {
    acc += touchRead(pin);
  }
  return acc * 0.25f;
}

static float proximity(float raw, float base, float span) {
  float p;
  if (TOUCH_FALLS_WHEN_NEAR) {
    p = (base - raw) / span;
  } else {
    p = (raw - base) / span;
  }
  if (p < 0) p = 0;
  if (p > 1) p = 1;
  // Ease the top so it is playable instead of a binary on/off.
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

  // Span is "how much the reading is allowed to move." Classic ESP32
  // idle values are often 40-80; a hand at a few cm can drop 15-40.
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
                USE_DAC ? "DAC" : "PWM");

  for (int i = 0; i < SINE_LEN; i++) {
    float th = (2.0f * PI * i) / SINE_LEN;
    sine[i] = (uint8_t)lroundf(127.5f + 127.0f * sinf(th));
  }

  if (USE_DAC) {
    dacWrite(PIN_AUDIO, 128);
  } else {
    // 8-bit PWM at a high carrier. Sounds harsher than the DAC path.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(PIN_AUDIO, 22050, 8);
    ledcWrite(PIN_AUDIO, 128);
#else
    ledcSetup(0, 22050, 8);
    ledcAttachPin(PIN_AUDIO, 0);
    ledcWrite(0, 128);
#endif
  }

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
    pSmooth = ema(pSmooth, p, 0.25f);
    vSmooth = ema(vSmooth, v, 0.25f);

    // Gate: ignore tiny noise so it rests silent.
    if (pSmooth < 0.04f) {
      freqHz = 0;
      amp = ema(amp, 0, 0.4f);
    } else {
      freqHz = FREQ_MIN_HZ * powf(FREQ_MAX_HZ / FREQ_MIN_HZ, pSmooth);
      amp = ema(amp, max(0.12f, vSmooth), 0.3f);
    }

    uint32_t inc = (uint32_t)((freqHz * (float)SINE_LEN / SAMPLE_HZ) * 65536.0f);
    phaseInc = inc;

    static uint32_t lastPrint = 0;
    if (nowMs - lastPrint >= 200) {
      lastPrint = nowMs;
      Serial.printf("p=%.2f v=%.2f  %6.1f Hz  rawP=%.0f rawV=%.0f\n",
                    pSmooth, vSmooth, freqHz,
                    touchNow(PIN_PITCH), touchNow(PIN_VOLUME));
    }
  }

  // Type 'c' in the serial monitor to recalibrate.
  if (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'c' || c == 'C') {
      calibrate();
    }
  }

  // Generate samples in the foreground. 16 kHz is light work for one core.
  uint32_t nowUs = micros();
  int guard = 0;
  while ((int32_t)(nowUs - nextSampleUs) >= 0 && guard++ < 16) {
    nextSampleUs += SAMPLE_US;
    phase += phaseInc;
    uint8_t idx = (uint8_t)((phase >> 16) & (SINE_LEN - 1));
    uint8_t s = sine[idx];
    int centered = (int)s - 128;
    int out = 128 + (int)(centered * amp);
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    if (USE_DAC) {
      dacWrite(PIN_AUDIO, (uint8_t)out);
    } else {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      ledcWrite(PIN_AUDIO, (uint8_t)out);
#else
      ledcWrite(0, (uint8_t)out);
#endif
    }
    nowUs = micros();
  }
}
