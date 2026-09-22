// S3 WiFi theremin
// Pitch antenna  -> GPIO4
// Volume antenna -> GPIO13
//
// The S3 makes a WiFi network. Your computer joins it and a browser
// page plays the tone on the PC speakers. No aux cable.
//
// Network:  S3-Theremin
// Password: theremin
// Page:     http://192.168.4.1
// Serial:   115200   type c to recalibrate

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

static const int PIN_PITCH  = 4;
static const int PIN_VOLUME = 13;

static const char *AP_SSID = "S3-Theremin";
static const char *AP_PASS = "theremin";

#if defined(CONFIG_IDF_TARGET_ESP32)
  static const bool TOUCH_FALLS_WHEN_NEAR = true;
#else
  static const bool TOUCH_FALLS_WHEN_NEAR = false;
#endif

static const float FREQ_MIN_HZ = 110.0f;
static const float FREQ_MAX_HZ = 880.0f;

static float pitchBase = 0;
static float volBase   = 0;
static float pitchSpan = 1;
static float volSpan   = 1;
static float freqHz    = 0;
static float amp       = 0;

WebServer server(80);

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>S3 Theremin</title>
<style>
  body { font-family: sans-serif; background:#111; color:#ddd; text-align:center; margin:2rem; }
  button { font-size:1.4rem; padding:0.8rem 1.4rem; }
  #hz { font-size:2.5rem; margin-top:1.2rem; }
  #hint { color:#888; margin-top:1rem; }
</style>
</head>
<body>
  <h1>S3 Theremin</h1>
  <button id="go">Tap to start sound</button>
  <div id="hz">— Hz</div>
  <div id="hint">Join WiFi S3-Theremin, then keep this page open.</div>
<script>
let ctx, osc, gain, running = false;
const hzEl = document.getElementById('hz');

document.getElementById('go').onclick = async () => {
  if (!running) {
    ctx = new AudioContext();
    osc = ctx.createOscillator();
    gain = ctx.createGain();
    osc.type = 'sine';
    gain.gain.value = 0;
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start();
    running = true;
    document.getElementById('go').textContent = 'Running';
  }
  if (ctx.state === 'suspended') await ctx.resume();
};

async function poll() {
  try {
    const r = await fetch('/state');
    const [f, a] = (await r.text()).split(',').map(Number);
    hzEl.textContent = (f > 1 ? f.toFixed(1) : '0') + ' Hz';
    if (running && ctx) {
      const freq = (f > 1) ? f : 110;
      const g = (f > 1) ? Math.max(0, Math.min(0.35, a)) : 0;
      osc.frequency.setTargetAtTime(freq, ctx.currentTime, 0.04);
      gain.gain.setTargetAtTime(g, ctx.currentTime, 0.04);
    }
  } catch (e) {}
  setTimeout(poll, 40);
}
poll();
</script>
</body>
</html>
)HTML";

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

static void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

static void handleState() {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f,%.3f", freqHz, amp);
  server.send(200, "text/plain", buf);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("S3 WiFi theremin");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  Serial.print("WiFi: ");
  Serial.println(AP_SSID);
  Serial.print("Pass: ");
  Serial.println(AP_PASS);
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.begin();

  calibrate();
}

void loop() {
  server.handleClient();

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

    static uint32_t lastPrint = 0;
    if (nowMs - lastPrint >= 250) {
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
}
